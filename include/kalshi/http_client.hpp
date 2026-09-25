#pragma once

#include "kalshi/environment.hpp"
#include "kalshi/error.hpp"
#include "kalshi/signer.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kalshi {

/// HTTP methods. `DEL` avoids the `DELETE` macro from `<windows.h>`; it is sent
/// as `DELETE`.
enum class HttpMethod : std::uint8_t { GET, POST, PUT, DEL };

[[nodiscard]] constexpr std::string_view to_string(HttpMethod method) noexcept {
	switch (method) {
		case HttpMethod::GET:
			return "GET";
		case HttpMethod::POST:
			return "POST";
		case HttpMethod::PUT:
			return "PUT";
		case HttpMethod::DEL:
			return "DELETE";
	}
	return "GET";
}

struct HttpResponse {
	int status_code{0};
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers;

	/// Case-insensitive header lookup. Returns the first match.
	[[nodiscard]] std::optional<std::string_view> header(std::string_view name) const noexcept;
};

struct ClientConfig {
	/// Base URL including `/trade-api/v2`. See rest_base_url().
	std::string base_url{rest_base_url(Environment::Production)};
	/// Limit for the whole request, including the response body.
	std::chrono::milliseconds timeout{std::chrono::seconds{30}};
	std::chrono::milliseconds connect_timeout{std::chrono::seconds{10}};
	bool verify_ssl{true};

	[[nodiscard]] static ClientConfig for_environment(Environment environment) {
		ClientConfig config;
		config.base_url = std::string(rest_base_url(environment));
		return config;
	}
};

/// Request boundary used by KalshiClient. Implement it to add logging,
/// retries, or rate limiting, or to test without a network.
class HttpTransport {
public:
	virtual ~HttpTransport() = default;

	/// `path` is relative to the base URL and may carry a query string.
	[[nodiscard]] virtual Result<HttpResponse> request(HttpMethod method, std::string_view path,
													   std::string_view body = {}) const = 0;

	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const {
		return request(HttpMethod::GET, path);
	}
	[[nodiscard]] Result<HttpResponse> post(std::string_view path,
											std::string_view body = {}) const {
		return request(HttpMethod::POST, path, body);
	}
	[[nodiscard]] Result<HttpResponse> put(std::string_view path,
										   std::string_view body = {}) const {
		return request(HttpMethod::PUT, path, body);
	}
	[[nodiscard]] Result<HttpResponse> del(std::string_view path,
										   std::string_view body = {}) const {
		return request(HttpMethod::DEL, path, body);
	}
};

/// libcurl transport. Signs every request when constructed with a Signer;
/// without one it sends unauthenticated requests, which is enough for public
/// market data.
///
/// Requests on one client run one at a time because they share a libcurl
/// handle and its connection cache. Use one client per thread for parallelism.
class HttpClient final : public HttpTransport {
public:
	explicit HttpClient(Signer signer, ClientConfig config = {});
	explicit HttpClient(ClientConfig config = {});
	~HttpClient() override;

	HttpClient(HttpClient&&) noexcept;
	HttpClient& operator=(HttpClient&&) noexcept;
	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;

	using HttpTransport::del;
	using HttpTransport::get;
	using HttpTransport::post;
	using HttpTransport::put;

	[[nodiscard]] Result<HttpResponse> request(HttpMethod method, std::string_view path,
											   std::string_view body = {}) const override;

	[[nodiscard]] const ClientConfig& config() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace kalshi
