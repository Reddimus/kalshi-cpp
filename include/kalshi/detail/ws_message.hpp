#pragma once

#include "kalshi/websocket.hpp"

#include <optional>
#include <string_view>

namespace kalshi::detail {

/// Parse one supported WebSocket data frame without dispatching callbacks.
[[nodiscard]] std::optional<WsMessage> parse_ws_data_message(std::string_view json);

} // namespace kalshi::detail
