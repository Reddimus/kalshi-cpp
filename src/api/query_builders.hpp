#pragma once

#include "kalshi/api.hpp"

#include <string>

namespace kalshi::api_detail {

// Pure function — emits the percent-encoded query string for the
// ``GET /series`` endpoint. Lives outside ``KalshiClient`` so the
// percent-encoding behavior is directly testable without the
// ``#define private public`` hack (which doesn't link on MSVC because
// the access modifier is part of the mangled symbol name on Windows).
[[nodiscard]] std::string build_series_query_string(const GetSeriesParams& params);

} // namespace kalshi::api_detail
