#pragma once

#include <string>
#include <string_view>

namespace kalshi::detail {

/// Drops object members whose value is null, at any depth, so they read as
/// absent; nulls inside arrays are kept. Kalshi sometimes sends null for fields
/// its specs mark non-null, so parsers retry with this after a parse fails.
[[nodiscard]] std::string strip_null_members(std::string_view json);

} // namespace kalshi::detail
