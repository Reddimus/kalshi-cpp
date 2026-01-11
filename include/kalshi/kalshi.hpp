#pragma once

/// @file kalshi.hpp
/// @brief Main include file for the Kalshi C++ SDK

#include "kalshi/api.hpp"
#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/models/market.hpp"
#include "kalshi/models/order.hpp"
#include "kalshi/pagination.hpp"
#include "kalshi/rate_limit.hpp"
#include "kalshi/retry.hpp"
#include "kalshi/signer.hpp"
#include "kalshi/websocket.hpp"

namespace kalshi {

/// SDK version
constexpr const char* VERSION = "0.1.0";

} // namespace kalshi
