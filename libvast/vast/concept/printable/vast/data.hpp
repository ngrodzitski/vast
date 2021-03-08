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

#include "vast/concept/printable/core/printer.hpp"
#include "vast/concept/printable/numeric.hpp"
#include "vast/concept/printable/print.hpp"
#include "vast/concept/printable/std/chrono.hpp"
#include "vast/concept/printable/string.hpp"
#include "vast/concept/printable/vast/address.hpp"
#include "vast/concept/printable/vast/none.hpp"
#include "vast/concept/printable/vast/pattern.hpp"
#include "vast/concept/printable/vast/subnet.hpp"
#include "vast/concept/printable/vast/type.hpp"
#include "vast/data.hpp"
#include "vast/detail/escapers.hpp"
#include "vast/detail/overload.hpp"
#include "vast/detail/string.hpp"

namespace vast {

struct data_printer : printer<data_printer> {
  using attribute = data;

  template <class Iterator>
  bool print(Iterator& out, const data& d) const {
    return caf::visit(
      detail::overload{
        [&](const auto& x) {
          return make_printer<std::decay_t<decltype(x)>>{}(out, x);
        },
        [&](integer x) {
          return printers::integral<integer, policy::force_sign>(out, x);
        },
        [&](const std::string& x) {
          static auto escaper = detail::make_extra_print_escaper("\"");
          static auto p = '"' << printers::escape(escaper) << '"';
          return p(out, x);
        },
      },
      d);
  }
};

// template <>
// struct printer_registry<data> {
//   using type = data_printer;
// };

// namespace printers {
//   auto const data = data_printer{};
// } // namespace printers

// struct vast_list_printer : printer<vast_list_printer> {
//   using attribute = list;

//   template <class Iterator>
//   bool print(Iterator& out, const list& xs) const {
//     auto p = '[' << ~(data_printer{} % ", ") << ']';
//     return p.print(out, xs);
//   }
// };

// template <>
// struct printer_registry<list> {
//   using type = vast_list_printer;
// };

// struct map_printer : printer<map_printer> {
//   using attribute = map;

//   template <class Iterator>
//   bool print(Iterator& out, const map& xs) const {
//     if (xs.empty())
//       return printers::str.print(out, "{}");
//     auto kvp = printers::data << " -> " << printers::data;
//     auto p = '{' << (kvp % ", ") << '}';
//     return p.print(out, xs);
//   }
// };

// template <>
// struct printer_registry<map> {
//   using type = map_printer;
// };

// struct record_printer : printer<record_printer> {
//   using attribute = record;

//   template <class Iterator>
//   bool print(Iterator& out, const record& xs) const {
//     auto kvp = printers::str << ": " << printers::data;
//     auto p = '<' << (kvp % ", ") << '>';
//     return p.print(out, xs);
//   }
// };

// template <>
// struct printer_registry<record> {
//   using type = record_printer;
// };

} // namespace vast
