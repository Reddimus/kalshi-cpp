#include "ws_endpoint.hpp"

#include <charconv>
#include <string>

namespace kalshi::detail {

Result<WsEndpoint> parse_ws_endpoint(std::string_view url) {
	bool use_ssl = false;
	std::string_view remainder;
	if (url.starts_with("wss://")) {
		use_ssl = true;
		remainder = url.substr(6);
	} else if (url.starts_with("ws://")) {
		remainder = url.substr(5);
	} else {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "WebSocket URL must use ws:// or wss://"});
	}

	const std::size_t path_pos = remainder.find_first_of("/?#");
	const std::string_view authority = remainder.substr(0, path_pos);
	if (authority.empty() || authority.find('@') != std::string_view::npos) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "WebSocket URL must contain a host"});
	}
	if (path_pos != std::string_view::npos && remainder[path_pos] == '#') {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "WebSocket URL fragments are not supported"});
	}

	std::string_view host;
	std::string_view port_text;
	if (authority.front() == '[') {
		const std::size_t closing_bracket = authority.find(']');
		if (closing_bracket == std::string_view::npos || closing_bracket == 1) {
			return std::unexpected(
				Error{ErrorCode::InvalidRequest, "WebSocket URL contains an invalid IPv6 host"});
		}
		host = authority.substr(1, closing_bracket - 1);
		const std::string_view suffix = authority.substr(closing_bracket + 1);
		if (!suffix.empty()) {
			if (!suffix.starts_with(':')) {
				return std::unexpected(
					Error{ErrorCode::InvalidRequest, "WebSocket URL contains an invalid host"});
			}
			port_text = suffix.substr(1);
		}
	} else {
		const std::size_t colon = authority.rfind(':');
		if (colon == std::string_view::npos) {
			host = authority;
		} else {
			if (authority.find(':') != colon) {
				return std::unexpected(
					Error{ErrorCode::InvalidRequest, "IPv6 WebSocket hosts must use brackets"});
			}
			host = authority.substr(0, colon);
			port_text = authority.substr(colon + 1);
		}
	}

	if (host.empty()) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "WebSocket URL must contain a host"});
	}
	for (const char character : host) {
		if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
			return std::unexpected(
				Error{ErrorCode::InvalidRequest, "WebSocket URL host contains whitespace"});
		}
	}

	int port = use_ssl ? 443 : 80;
	if (!port_text.empty()) {
		unsigned int parsed_port = 0;
		const auto [end, error] =
			std::from_chars(port_text.data(), port_text.data() + port_text.size(), parsed_port);
		if (error != std::errc{} || end != port_text.data() + port_text.size() ||
			parsed_port == 0 || parsed_port > 65535) {
			return std::unexpected(
				Error{ErrorCode::InvalidRequest, "WebSocket URL contains an invalid port"});
		}
		port = static_cast<int>(parsed_port);
	} else if (authority.ends_with(':')) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "WebSocket URL contains an invalid port"});
	}

	std::string path{"/"};
	if (path_pos != std::string_view::npos) {
		const std::string_view suffix = remainder.substr(path_pos);
		path =
			suffix.starts_with('?') ? std::string{"/"} + std::string{suffix} : std::string{suffix};
		if (path.find('#') != std::string::npos) {
			return std::unexpected(
				Error{ErrorCode::InvalidRequest, "WebSocket URL fragments are not supported"});
		}
	}

	return WsEndpoint{std::string{host}, std::move(path), port, use_ssl};
}

} // namespace kalshi::detail
