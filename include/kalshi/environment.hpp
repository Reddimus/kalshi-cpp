#pragma once

#include <cstdint>
#include <string_view>

namespace kalshi {

/// Kalshi deployment. Demo keys work only against demo hosts, and production
/// keys only against production hosts.
enum class Environment : std::uint8_t { Production, Demo };

/// REST base URL, including the `/trade-api/v2` prefix that requests sign.
[[nodiscard]] constexpr std::string_view rest_base_url(Environment environment) noexcept {
	return environment == Environment::Demo ? "https://external-api.demo.kalshi.co/trade-api/v2"
											: "https://external-api.kalshi.com/trade-api/v2";
}

/// WebSocket URL for streaming market and account data.
[[nodiscard]] constexpr std::string_view websocket_url(Environment environment) noexcept {
	return environment == Environment::Demo ? "wss://external-api-ws.demo.kalshi.co/trade-api/ws/v2"
											: "wss://external-api-ws.kalshi.com/trade-api/ws/v2";
}

} // namespace kalshi
