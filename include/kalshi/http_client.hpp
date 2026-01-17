#pragma once

#include "kalshi/error.hpp"
#include "kalshi/signer.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kalshi {

/// HTTP methods
enum class HttpMethod : std::uint8_t { GET, POST, PUT, DELETE };

/// Convert HTTP method to string
[[nodiscard]] constexpr std::string_view to_string(HttpMethod method) noexcept {
	switch (method) {
		case HttpMethod::GET:
			return "GET";
		case HttpMethod::POST:
			return "POST";
		case HttpMethod::PUT:
			return "PUT";
		case HttpMethod::DELETE:
			return "DELETE";
	}
	return "GET";
}

/// HTTP response
/// Uses contiguous vector storage for headers instead of unordered_map
/// for better cache locality (typically <10 headers in a response).
struct HttpResponse {
	std::int16_t status_code; // HTTP status codes fit in int16 (100-599)
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers;
};

/// HTTP client configuration
struct ClientConfig {
	std::string base_url{"https://api.elections.kalshi.com/trade-api/v2"};
	std::chrono::seconds timeout{30};
	bool verify_ssl{true};
};

/// HTTP client for Kalshi API
///
/// @note Thread Safety: This class is NOT thread-safe. The underlying CURL
/// handle is shared across all requests. If you need concurrent API access,
/// create one HttpClient instance per thread or protect access with a mutex.
class HttpClient {
public:
	/// Create a client with the given signer and configuration
	HttpClient(Signer signer, ClientConfig config = {});
	~HttpClient();

	HttpClient(HttpClient&&) noexcept;
	HttpClient& operator=(HttpClient&&) noexcept;

	// Non-copyable
	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;

	/// Perform a GET request
	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const;

	/// Perform a POST request
	[[nodiscard]] Result<HttpResponse> post(std::string_view path,
											std::string_view body = {}) const;

	/// Perform a PUT request
	[[nodiscard]] Result<HttpResponse> put(std::string_view path, std::string_view body = {}) const;

	/// Perform a DELETE request
	[[nodiscard]] Result<HttpResponse> del(std::string_view path) const;

	/// Perform a request with custom method
	[[nodiscard]] Result<HttpResponse> request(HttpMethod method, std::string_view path,
											   std::string_view body = {}) const;

	/// Get the client configuration
	[[nodiscard]] const ClientConfig& config() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace kalshi
