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

#pragma once

#include "vast/fwd.hpp"

#include "vast/aliases.hpp"
#include "vast/data.hpp"
#include "vast/path.hpp"
#include "vast/system/actors.hpp"
#include "vast/system/instrumentation.hpp"

#include <caf/typed_event_based_actor.hpp>
#include <caf/typed_response_promise.hpp>

#include <chrono>
#include <vector>

namespace vast::system {

/// Receives chunks from SOURCEs, imbues them with an ID, and relays them to
/// ARCHIVE, INDEX and continuous queries.
struct importer_state {
  // -- member types -----------------------------------------------------------

  /// Used to signal how much information should be persisted in write_state().
  enum class write_mode : bool {
    /// Persist the next assignable id, used during a regular shutdown.
    with_next,
    /// Persist only the end of the block, used during regular operation to
    /// prevent state corruption if an irregular shutdown occurs.
    without_next
  };

  /// A helper structure to partition the id space into blocks.
  /// An importer uses one currently active block.
  struct id_block {
    /// The next available id of this block.
    id next;

    /// The last + 1 id of this block.
    id end;
  };

  explicit importer_state(importer_actor::pointer self);

  ~importer_state();

  caf::error read_state();

  caf::error write_state(write_mode mode);

  void send_report();

  /// Extends the available ids by block size
  /// @param required The minimum increment of ids so that available ids are
  /// not depleted after calling this function and assigning this amount
  /// subsequently.
  caf::error get_next_block(uint64_t required = 0u);

  /// @returns the next unused id and increments the position by its argument.
  id next_id(uint64_t advance);

  /// @returns the number of currently available IDs.
  id available_ids() const noexcept;

  /// @returns various status metrics.
  caf::typed_response_promise<caf::settings> status(status_verbosity v) const;

  /// The active id block.
  id_block current;

  /// State directory.
  path dir;

  /// All available ANALYZER PLUGIN actors and their names.
  std::vector<std::pair<std::string, analyzer_plugin_actor>> analyzers;

  /// The continous stage that moves data from all sources to all subscribers.
  caf::stream_stage_ptr<table_slice,
                        caf::broadcast_downstream_manager<table_slice>>
    stage;

  /// Pointer to the owning actor.
  importer_actor::pointer self;

  std::string inbound_description = "anonymous";

  std::unordered_map<caf::inbound_path*, std::string> inbound_descriptions;

  measurement measurement_;
  stopwatch::time_point last_report;

  /// The index actor.
  index_actor index;

  accountant_actor accountant;

  /// Name of this actor in log events.
  static inline const char* name = "importer";
};

/// Spawns an IMPORTER.
/// @param self The actor handle.
/// @param dir The directory for persistent state.
/// @param max_table_slice_size The suggested maximum size for table slices.
/// @param node A pointer to the to the NODE actor handle.
/// @param archive A handle to the ARCHIVE.
/// @param index A handle to the INDEX.
/// @param batch_size The initial number of IDs to request when replenishing.
/// @param type_registry A handle to the type-registry module.
importer_actor::behavior_type
importer(importer_actor::stateful_pointer<importer_state> self, path dir,
         node_actor::pointer node, const archive_actor& archive,
         index_actor index, const type_registry_actor& type_registry);

} // namespace vast::system
