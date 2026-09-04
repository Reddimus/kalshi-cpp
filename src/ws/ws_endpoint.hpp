#pragma once

#include "kalshi/error.hpp"

#include <string>
#include <string_view>

namespace kalshi::detail {

struct WsEndpoint {
	std::string host;
	std::string path;
	int port{0};
	bool use_ssl{false};
};

[[nodiscard]] Result<WsEndpoint> parse_ws_endpoint(std::string_view url);

} // namespace kalshi::detail
