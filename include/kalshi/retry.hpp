#pragma once

#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

namespace kalshi {

struct RetryPolicy {
	std::chrono::milliseconds initial_delay{100};
	std::chrono::milliseconds max_delay{std::chrono::seconds{10}};
	double backoff_multiplier{2.0};
	/// Random spread applied to each delay, as a fraction of it. 0 disables jitter.
	double jitter_factor{0.1};
	/// Total attempts, including the first.
	std::uint8_t max_attempts{3};
	bool retry_on_network_error{true};
	bool retry_on_rate_limit{true};
	bool retry_on_server_error{true};
	/// Also retry POST after network errors and 5xx responses. Off by default:
	/// the first attempt may have reached the exchange, for example as a placed
	/// order. A 429 always retries because the server rejected the request.
	bool retry_non_idempotent{false};
};

[[nodiscard]] constexpr bool is_idempotent(HttpMethod method) noexcept {
	return method != HttpMethod::POST;
}

[[nodiscard]] bool should_retry(HttpMethod method, const HttpResponse& response,
								const RetryPolicy& policy) noexcept;
[[nodiscard]] bool should_retry(HttpMethod method, const Error& error,
								const RetryPolicy& policy) noexcept;

/// Backoff before the attempt after `attempt` (1-based), with jitter applied.
[[nodiscard]] std::chrono::milliseconds retry_delay(std::uint8_t attempt,
													const RetryPolicy& policy);

/// Transport decorator that retries transient failures with exponential
/// backoff. It honors a `Retry-After` header when the server sends one.
///
///     auto http = std::make_shared<kalshi::HttpClient>(signer);
///     kalshi::KalshiClient client{std::make_shared<kalshi::RetryingTransport>(http)};
class RetryingTransport final : public HttpTransport {
public:
	explicit RetryingTransport(std::shared_ptr<const HttpTransport> inner, RetryPolicy policy = {});

	[[nodiscard]] Result<HttpResponse> request(HttpMethod method, std::string_view path,
											   std::string_view body = {}) const override;

	[[nodiscard]] const RetryPolicy& policy() const noexcept { return policy_; }

private:
	std::shared_ptr<const HttpTransport> inner_;
	RetryPolicy policy_;
};

} // namespace kalshi
