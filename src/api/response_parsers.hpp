#pragma once

#include "kalshi/api.hpp"

#include <string_view>
#include <vector>

namespace kalshi::api_detail {

[[nodiscard]] std::vector<Candlestick> parse_candlesticks_response(std::string_view body);

} // namespace kalshi::api_detail
