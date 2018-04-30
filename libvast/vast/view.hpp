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

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include <caf/intrusive_ptr.hpp>
#include <caf/make_counted.hpp>
#include <caf/ref_counted.hpp>

#include "vast/aliases.hpp"
#include "vast/data.hpp"
#include "vast/time.hpp"

#include "vast/detail/operators.hpp"
#include "vast/detail/type_traits.hpp"

namespace vast {

/// A type-safe overlay over an immutable sequence of bytes.
template <class>
struct view;

/// @relates view
template <class T>
using view_t = typename view<T>::type;

/// @relates view
template <>
struct view<boolean> {
  using type = boolean;
};

/// @relates view
template <>
struct view<integer> {
  using type = integer;
};

/// @relates view
template <>
struct view<count> {
  using type = count;
};

/// @relates view
template <>
struct view<real> {
  using type = real;
};

/// @relates view
template <>
struct view<timespan> {
  using type = timespan;
};

/// @relates view
template <>
struct view<timestamp> {
  using type = timestamp;
};

/// @relates view
template <>
struct view<std::string> {
  using type = std::string_view;
};

/// @relates view
class pattern_view : detail::totally_ordered<pattern_view> {
public:
  static pattern glob(std::string_view x);

  pattern_view(const pattern& x);

  bool match(std::string_view x) const;
  bool search(std::string_view x) const;
  std::string_view string() const;

  friend bool operator==(pattern_view x, pattern_view y) noexcept;
  friend bool operator<(pattern_view x, pattern_view y) noexcept;

private:
  std::string_view pattern_;
};

//// @relates view
template <>
struct view<pattern> {
  using type = pattern_view;
};

/// @relates view
class address_view : detail::totally_ordered<address_view> {
public:
  address_view(const address& x);

  // See vast::address for documentation.
  bool is_v4() const;
  bool is_v6() const;
  bool is_loopback() const;
  bool is_broadcast() const;
  bool is_multicast() const;
  bool mask(unsigned top_bits_to_keep) const;
  bool compare(address_view other, size_t k) const;
  const std::array<uint8_t, 16>& data() const;

  friend bool operator==(address_view x, address_view y) noexcept;
  friend bool operator<(address_view x, address_view y) noexcept;

private:
  const std::array<uint8_t, 16>* data_;
};

//// @relates view
template <>
struct view<address> {
  using type = address_view;
};

/// @relates view
class subnet_view : detail::totally_ordered<subnet_view> {
public:
  subnet_view(const subnet& x);

  // See vast::subnet for documentation.
  bool contains(address_view x) const;
  bool contains(subnet_view x) const;
  address_view network() const;
  uint8_t length() const;

  friend bool operator==(subnet_view x, subnet_view y) noexcept;
  friend bool operator<(subnet_view x, subnet_view y) noexcept;

private:
  address_view network_;
  uint8_t length_;
};

//// @relates view
template <>
struct view<subnet> {
  using type = subnet_view;
};

//// @relates view
template <>
struct view<port> {
  using type = port;
};

// @relates view
struct vector_view;

/// @relates view
using vector_view_ptr = caf::intrusive_ptr<vector_view>;

/// @relates view
template <>
struct view<vector> {
  using type = vector_view_ptr;
};

/// A type-erased view over variout types of data.
/// @relates view
using data_view_variant = std::variant<
  view_t<boolean>,
  view_t<integer>,
  view_t<count>,
  view_t<real>,
  view_t<timespan>,
  view_t<timestamp>,
  view_t<std::string>,
  view_t<pattern>,
  view_t<address>,
  view_t<subnet>,
  view_t<port>,
  view_t<vector>
>;

/// @relates view
template <>
struct view<data> {
  using type = data_view_variant;
};

/// @relates view
struct vector_view : public caf::ref_counted {
  using value_type = view_t<data>;
  using size_type = size_t;

  virtual ~vector_view() = default;

  /// Retrieves a specific element.
  /// @param i The position of the element to retrieve.
  /// @returns A view to the element at position *i*.
  virtual value_type at(size_type i) const = 0;

  /// @returns The number of elements in the container.
  virtual size_type size() const noexcept = 0;
};

/// A view over a @ref vector.
/// @relates view
class default_vector_view
  : public vector_view,
    detail::totally_ordered<default_vector_view> {
public:
  default_vector_view(const vector& xs);

  value_type at(size_type i) const override;

  size_type size() const noexcept override;

private:
  const vector& xs_;
};

/// Creates a type-erased data view from a specific type.
/// @relates view
template <class T>
view_t<data> make_view(const T& x) {
  constexpr auto directly_constructible
    = detail::is_any_v<T, boolean, integer, count, real, timespan,
                       timestamp, std::string, pattern, address, subnet, port>;
  if constexpr (directly_constructible) {
    return view_t<data>{x};
  } else if constexpr (std::is_same_v<T, vector>) {
    return view_t<vector>{caf::make_counted<default_vector_view>(x)};
  } else if constexpr (std::is_same_v<T, set>) {
    return {}; // TODO
  } else if constexpr (std::is_same_v<T, table>) {
    return {}; // TODO
  } else {
    return {};
  }
}

view_t<data> make_view(const data& x);

} // namespace vast
