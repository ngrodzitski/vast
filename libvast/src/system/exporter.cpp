/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

#include "vast/system/exporter.hpp"

#include "vast/fwd.hpp"

#include "vast/concept/printable/std/chrono.hpp"
#include "vast/concept/printable/to_string.hpp"
#include "vast/concept/printable/vast/bitmap.hpp"
#include "vast/concept/printable/vast/expression.hpp"
#include "vast/concept/printable/vast/uuid.hpp"
#include "vast/detail/assert.hpp"
#include "vast/detail/fill_status_map.hpp"
#include "vast/detail/narrow.hpp"
#include "vast/error.hpp"
#include "vast/expression_visitors.hpp"
#include "vast/logger.hpp"
#include "vast/system/query_status.hpp"
#include "vast/system/report.hpp"
#include "vast/system/status_verbosity.hpp"
#include "vast/table_slice.hpp"

#include <caf/settings.hpp>
#include <caf/stream_slot.hpp>
#include <caf/typed_event_based_actor.hpp>

namespace vast::system {

namespace {

bool finished(const query_status& qs) {
  return qs.received == qs.expected && qs.lookups_issued == qs.lookups_complete;
}

void ship_results(exporter_actor::stateful_pointer<exporter_state> self) {
  VAST_TRACE_SCOPE("");
  auto& st = self->state;
  VAST_DEBUG("{} relays {} events", self, st.query.cached);
  while (st.query.requested > 0 && st.query.cached > 0) {
    VAST_ASSERT(!st.results.empty());
    // Fetch the next table slice. Either we grab the entire first slice in
    // st.results or we need to split it up.
    table_slice slice = {};
    if (st.results[0].rows() <= st.query.requested) {
      slice = std::move(st.results[0]);
      st.results.erase(st.results.begin());
    } else {
      auto [first, second] = split(st.results[0], st.query.requested);
      VAST_ASSERT(first.encoding() != table_slice_encoding::none);
      VAST_ASSERT(second.encoding() != table_slice_encoding::none);
      VAST_ASSERT(first.rows() == st.query.requested);
      slice = std::move(first);
      st.results[0] = std::move(second);
    }
    // Ship the slice and update state.
    auto rows = slice.rows();
    VAST_ASSERT(rows <= st.query.cached);
    st.query.cached -= rows;
    st.query.requested -= rows;
    st.query.shipped += rows;
    self->anon_send(st.sink, std::move(slice));
  }
}

void report_statistics(exporter_actor::stateful_pointer<exporter_state> self) {
  auto& st = self->state;
  if (st.statistics_subscriber)
    self->anon_send(st.statistics_subscriber, st.name, st.query);
  if (st.accountant) {
    auto hits = rank(st.hits);
    auto processed = st.query.processed;
    auto shipped = st.query.shipped;
    auto results = shipped + st.results.size();
    auto selectivity = double(results) / processed;
    auto msg = report{
      {"exporter.hits", hits},
      {"exporter.processed", processed},
      {"exporter.results", results},
      {"exporter.shipped", shipped},
      {"exporter.selectivity", selectivity},
      {"exporter.runtime", st.query.runtime},
    };
    self->send(st.accountant, msg);
  }
}

void shutdown(exporter_actor::stateful_pointer<exporter_state> self,
              caf::error err) {
  VAST_DEBUG("{} initiates shutdown with error {}", self, render(err));
  self->send_exit(self, std::move(err));
}

void shutdown(exporter_actor::stateful_pointer<exporter_state> self) {
  if (has_continuous_option(self->state.options))
    return;
  VAST_DEBUG("{} initiates shutdown", self);
  self->send_exit(self, caf::exit_reason::normal);
}

void request_more_hits(exporter_actor::stateful_pointer<exporter_state> self) {
  auto& st = self->state;
  // Sanity check.
  if (!has_historical_option(st.options)) {
    VAST_WARN("{} requested more hits for continuous query", self);
    return;
  }
  // Do nothing if we already shipped everything the client asked for.
  if (st.query.requested == 0) {
    VAST_DEBUG("{} shipped {} results and waits for client to request "
               "more",
               self, self->state.query.shipped);
    return;
  }
  // Do nothing if we are still waiting for results from the ARCHIVE.
  if (st.query.lookups_issued > st.query.lookups_complete) {
    VAST_DEBUG("{} currently awaits {} more lookup results from the "
               "archive",
               self, st.query.lookups_issued - st.query.lookups_complete);
    return;
  }
  // If the if-statement above isn't true then the two values must be equal.
  // Otherwise, we would complete more than we issue.
  VAST_ASSERT(st.query.lookups_issued == st.query.lookups_complete);
  // Do nothing if we received everything.
  if (st.query.received == st.query.expected) {
    VAST_DEBUG("{} received hits for all {} partitions", self,
               st.query.expected);
    return;
  }
  // If the if-statement above isn't true then `received < expected` must hold.
  // Otherwise, we would receive results for more partitions than qualified as
  // hits by the INDEX.
  VAST_ASSERT(st.query.received < st.query.expected);
  auto remaining = st.query.expected - st.query.received;
  // TODO: Figure out right number of partitions to ask for. For now, we
  // bound the number by an arbitrary constant.
  auto n = std::min(remaining, size_t{2});
  // Store how many partitions we schedule with our request. When receiving
  // 'done', we add this number to `received`.
  st.query.scheduled = n;
  // Request more hits from the INDEX.
  VAST_DEBUG("{} asks index to process {} more partitions", self, n);
  self->send(st.index, st.id, detail::narrow<uint32_t>(n));
}

void handle_batch(exporter_actor::stateful_pointer<exporter_state> self,
                  table_slice slice) {
  VAST_ASSERT(slice.encoding() != table_slice_encoding::none);
  VAST_DEBUG("{} got batch of {} events", self, slice.rows());
  // Construct a candidate checker if we don't have one for this type.
  type t = slice.layout();
  auto it = self->state.checkers.find(t);
  if (it == self->state.checkers.end()) {
    auto x = tailor(self->state.expr, t);
    if (!x) {
      VAST_ERROR("{} failed to tailor expression: {}", self, render(x.error()));
      ship_results(self);
      shutdown(self);
      return;
    }
    VAST_DEBUG("{} tailored AST to {}: {}", self, t, x);
    std::tie(it, std::ignore)
      = self->state.checkers.emplace(type{slice.layout()}, std::move(*x));
  }
  auto& checker = it->second;
  // Perform candidate check, splitting the slice into subsets if needed.
  self->state.query.processed += slice.rows();
  auto selection = evaluate(checker, slice);
  auto selection_size = rank(selection);
  if (selection_size == 0) {
    // No rows qualify.
    return;
  }
  self->state.query.cached += selection_size;
  select(self->state.results, slice, selection);
  // Ship slices to connected SINKs.
  ship_results(self);
}

} // namespace

exporter_actor::behavior_type
exporter(exporter_actor::stateful_pointer<exporter_state> self, expression expr,
         query_options options) {
  self->state.options = options;
  self->state.expr = std::move(expr);
  if (has_continuous_option(options))
    VAST_DEBUG("{} has continuous query option", self);
  self->set_exit_handler([=](const caf::exit_msg& msg) {
    VAST_DEBUG("{} received exit from {} with reason: {}", self, msg.source,
               msg.reason);
    auto& st = self->state;
    if (msg.reason != caf::exit_reason::kill)
      report_statistics(self);
    // Sending 0 to the index means dropping further results.
    self->send<caf::message_priority::high>(st.index, st.id,
                                            static_cast<uint32_t>(0));
    self->quit(msg.reason);
  });
  self->set_down_handler([=](const caf::down_msg& msg) {
    VAST_DEBUG("{} received DOWN from {}", self, msg.source);
    if (has_continuous_option(self->state.options)
        && (msg.source == self->state.archive
            || msg.source == self->state.index))
      report_statistics(self);
    // Without sinks and resumable sessions, there's no reason to proceed.
    self->quit(msg.reason);
  });
  return {
    [self](atom::extract) -> caf::result<void> {
      // Sanity check.
      VAST_DEBUG("{} got request to extract all events", self);
      if (self->state.query.requested == max_events) {
        VAST_WARN("{} ignores extract request, already getting all", self);
        return {};
      }
      // Configure state to get all remaining partition results.
      self->state.query.requested = max_events;
      ship_results(self);
      request_more_hits(self);
      return {};
    },
    [self](atom::extract, uint64_t requested_results) -> caf::result<void> {
      // Sanity checks.
      if (requested_results == 0) {
        VAST_WARN("{} ignores extract request for 0 results", self);
        return {};
      }
      if (self->state.query.requested == max_events) {
        VAST_WARN("{} ignores extract request, already getting all", self);
        return {};
      }
      VAST_ASSERT(self->state.query.requested < max_events);
      // Configure state to get up to `requested_results` more events.
      auto n = std::min(max_events - requested_results, requested_results);
      VAST_DEBUG("{} got a request to extract {} more results in "
                 "addition to {} pending results",
                 self, n, self->state.query.requested);
      self->state.query.requested += n;
      ship_results(self);
      request_more_hits(self);
      return {};
    },
    [self](accountant_actor accountant) {
      self->state.accountant = std::move(accountant);
      self->send(self->state.accountant, atom::announce_v, self->name());
    },
    [self](archive_actor archive) {
      VAST_DEBUG("{} registers archive {}", self, archive);
      self->state.archive = std::move(archive);
      if (has_continuous_option(self->state.options))
        self->monitor(self->state.archive);
      // Register self at the archive
      if (has_historical_option(self->state.options))
        self->send(self->state.archive, atom::exporter_v,
                   caf::actor_cast<caf::actor>(self));
    },
    [self](index_actor index) {
      VAST_DEBUG("{} registers index {}", self, index);
      self->state.index = std::move(index);
      if (has_continuous_option(self->state.options))
        self->monitor(self->state.index);
    },
    [self](atom::sink, const caf::actor& sink) {
      VAST_DEBUG("{} registers sink {}", self, sink);
      self->state.sink = sink;
      self->monitor(self->state.sink);
    },
    [self](atom::run) {
      VAST_VERBOSE("{} executes query: {}", self, to_string(self->state.expr));
      self->state.start = std::chrono::system_clock::now();
      if (!has_historical_option(self->state.options))
        return;
      // TODO: The index replies to expressions by manually sending back to the
      // sender, which does not work with request(...).then(...) style of
      // communication for typed actors. Hence, we must actor_cast here.
      // Ideally, we would change that index handler to actually return the
      // desired value.
      self
        ->request(caf::actor_cast<caf::actor>(self->state.index), caf::infinite,
                  self->state.expr)
        .then(
          [=](const uuid& lookup, uint32_t partitions, uint32_t scheduled) {
            VAST_VERBOSE("{} got lookup handle {}, scheduled {}/{} partitions",
                         self, lookup, scheduled, partitions);
            self->state.id = lookup;
            if (partitions > 0) {
              self->state.query.expected = partitions;
              self->state.query.scheduled = scheduled;
            } else {
              shutdown(self);
            }
          },
          [=](const caf::error& e) { shutdown(self, e); });
    },
    [self](atom::statistics, const caf::actor& statistics_subscriber) {
      VAST_DEBUG("{} registers statistics subscriber {}", self,
                 statistics_subscriber);
      self->state.statistics_subscriber = statistics_subscriber;
    },
    [self](
      caf::stream<table_slice> in) -> caf::inbound_stream_slot<table_slice> {
      return self
        ->make_sink(
          in,
          [](caf::unit_t&) {
            // nop
          },
          [=](caf::unit_t&, table_slice slice) {
            handle_batch(self, std::move(slice));
          },
          [=](caf::unit_t&, const caf::error& err) {
            if (err)
              VAST_ERROR("{} got error during streaming: {}", self, err);
          })
        .inbound_slot();
    },
    // -- status_client_actor --------------------------------------------------
    [self](atom::status, status_verbosity v) {
      auto result = caf::settings{};
      auto& exporter_status = put_dictionary(result, "exporter");
      if (v >= status_verbosity::info) {
        caf::settings exp;
        put(exp, "expression", to_string(self->state.expr));
        auto& xs = put_list(result, "queries");
        xs.emplace_back(std::move(exp));
      }
      if (v >= status_verbosity::detailed) {
        caf::settings exp;
        put(exp, "expression", to_string(self->state.expr));
        put(exp, "hits", rank(self->state.hits));
        put(exp, "start", caf::deep_to_string(self->state.start));
        auto& xs = put_list(result, "queries");
        xs.emplace_back(std::move(exp));
        detail::fill_status_map(exporter_status, self);
      }
      return result;
    },
    // -- archive_client_actor -------------------------------------------------
    [self](table_slice slice) { //
      handle_batch(self, std::move(slice));
    },
    [self](atom::done, const caf::error& err) {
      VAST_ASSERT(self->current_sender() == self->state.archive);
      ++self->state.query.lookups_complete;
      VAST_DEBUG("{} received done from archive: {} {}", self, VAST_ARG(err),
                 VAST_ARG("query", self->state.query));
      // We skip 'done' messages of the query supervisors until we process all
      // hits first. Hence, we can never be finished here.
      VAST_ASSERT(!finished(self->state.query));
    },
    // -- index_client_actor ---------------------------------------------------
    // The INDEX (or the EVALUATOR, to be more precise) sends us a series of
    // `ids` in response to an expression (query), terminated by 'done'.
    [self](const ids& hits) -> caf::result<void> {
      // Skip results that arrive before we got our lookup handle from the
      // INDEX actor.
      if (self->state.query.expected == 0)
        return caf::skip;
      // Add `hits` to the total result set and update all stats.
      caf::timespan runtime
        = std::chrono::system_clock::now() - self->state.start;
      self->state.query.runtime = runtime;
      auto count = rank(hits);
      if (self->state.accountant) {
        auto r = report{};
        if (self->state.hits.empty())
          r.push_back({"exporter.hits.first", runtime});
        r.push_back({"exporter.hits.arrived", runtime});
        r.push_back({"exporter.hits.count", count});
        self->send(self->state.accountant, r);
      }
      if (count == 0) {
        VAST_WARN("{} got empty hits", self);
      } else {
        VAST_ASSERT(rank(self->state.hits & hits) == 0);
        VAST_DEBUG("{} got {} index hits in [{}, {})", self, count,
                   select(hits, 1), (select(hits, -1) + 1));
        self->state.hits |= hits;
        VAST_DEBUG("{} forwards hits to archive", self);
        // FIXME: restrict according to configured limit.
        ++self->state.query.lookups_issued;
        self->send(self->state.archive, std::move(hits),
                   static_cast<archive_client_actor>(self));
      }
      return {};
    },
    [self](atom::done) -> caf::result<void> {
      // Ignore this message until we got all lookup results from the ARCHIVE.
      // Otherwise, we can end up in weirdly interleaved state.
      if (self->state.query.lookups_issued
          != self->state.query.lookups_complete)
        return caf::skip;
      // Figure out if we're done by bumping the counter for `received` and
      // check whether it reaches `expected`.
      caf::timespan runtime
        = std::chrono::system_clock::now() - self->state.start;
      self->state.query.runtime = runtime;
      self->state.query.received += self->state.query.scheduled;
      if (self->state.query.received < self->state.query.expected) {
        VAST_DEBUG("{} received hits from {}/{} partitions", self,
                   self->state.query.received, self->state.query.expected);
        request_more_hits(self);
      } else {
        VAST_DEBUG("{} received all hits from {} partition(s) in {}", self,
                   self->state.query.expected, vast::to_string(runtime));
        if (self->state.accountant)
          self->send(self->state.accountant, "exporter.hits.runtime", runtime);
        if (finished(self->state.query))
          shutdown(self);
      }
      return {};
    },
  };
}

} // namespace vast::system
