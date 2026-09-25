#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace kalshi {

enum class ErrorCode : std::uint8_t {
	NetworkError,		 ///< The request did not complete (DNS, TLS, timeout, reset).
	AuthenticationError, ///< HTTP 401 or 403.
	InvalidRequest,		 ///< Rejected before sending, or HTTP 400, 409, or 422.
	NotFound,			 ///< HTTP 404.
	RateLimited,		 ///< HTTP 429, or a local rate limit that would wait too long.
	ServerError,		 ///< HTTP 5xx.
	ParseError,			 ///< The response was not the JSON the API documents.
	SigningError,		 ///< The request could not be signed.
	InvalidKey,			 ///< The private key could not be loaded.
	Unknown
};

struct Error {
	ErrorCode code{ErrorCode::Unknown};
	std::string message;
	/// HTTP status when the server answered; 0 otherwise.
	int http_status{0};
	/// Kalshi's machine-readable error code, such as "market_not_found", when sent.
	std::string api_code;

	[[nodiscard]] static Error network(std::string msg) {
		return {ErrorCode::NetworkError, std::move(msg)};
	}

	[[nodiscard]] static Error auth(std::string msg) {
		return {ErrorCode::AuthenticationError, std::move(msg)};
	}

	[[nodiscard]] static Error parse(std::string msg) {
		return {ErrorCode::ParseError, std::move(msg)};
	}

	[[nodiscard]] static Error signing(std::string msg) {
		return {ErrorCode::SigningError, std::move(msg)};
	}
};

/// Result type for SDK operations
template <typename T>
using Result = std::expected<T, Error>;

} // namespace kalshi
