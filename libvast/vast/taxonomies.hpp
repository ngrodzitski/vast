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

#include <caf/meta/type_name.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vast {

/// Maps concept names to the fields or concepts that implement them.
using concepts_type = std::unordered_map<std::string, std::vector<std::string>>;

/// Maps model names to the concepts or models from which they are constituted.
using models_type = std::unordered_map<std::string, std::vector<std::string>>;

/// A taxonomy is a combination of concepts and models. VAST stores all
/// configured taxonomies in memory together, hence the plural naming.
struct taxonomies {
  concepts_type concepts;
  models_type models;

  friend bool operator==(const taxonomies& lhs, const taxonomies& rhs);

  template <class Inspector>
  friend auto inspect(Inspector& f, taxonomies& t) {
    return f(caf::meta::type_name("taxonomies"), t.concepts, t.models);
  }
};

/// The taxonomies_ptr is used to hand out pointers to the taxonomies and let
/// the receiver co-own them for the time they need to.
using taxonomies_ptr = std::shared_ptr<const taxonomies>;

// Required to put a taxonomies_ptr into a caf::message.
/// @relates taxonomies_ptr
caf::error inspect(caf::serializer& sink, const taxonomies_ptr& x);

// Required to put a taxonomies_ptr into a caf::message.
/// @relates taxonomies_ptr
caf::error inspect(caf::deserializer& source, taxonomies_ptr& x);

/// Substitutes concept and model identifiers in field extractors with
/// replacement expressions containing only concrete field names.
/// @param t The set of taxonomies to apply.
/// @param e The original expression.
/// @returns The sustitute expression.
expression resolve(const taxonomies& t, const expression& e);

} // namespace vast
