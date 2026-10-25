#include "kalshi/retry.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <random>
#include <thread>

namespace kalshi {

namespace {

/// Parses a delta-seconds Retry-After value. HTTP dates are ignored.
std::optional<std::chrono::milliseconds> retry_after(const HttpResponse& response) {
	const std::optional<std::string_view> value = response.header("Retry-After");
	if (!value) {
		return std::nullopt;
	}
	std::int64_t seconds = 0;
	const char* end = value->data() + value->size();
	const std::from_chars_result parsed = std::from_chars(value->data(), end, seconds);
	if (parsed.ec != std::errc{} || parsed.ptr != end || seconds < 0) {
		return std::nullopt;
	}
	return std::chrono::seconds{std::min<std::int64_t>(seconds, 3600)};
}

} // namespace

bool should_retry(HttpMethod method, const HttpResponse& response,
				  const RetryPolicy& policy) noexcept {
	if (response.status_code == 429) {
		return policy.retry_on_rate_limit;
	}
	const bool may_repeat = method == HttpMethod::GET || policy.retry_writes;
	return policy.retry_on_server_error && may_repeat && response.status_code >= 500 &&
		   response.status_code <= 599;
}

bool should_retry(HttpMethod method, const Error& error, const RetryPolicy& policy) noexcept {
	const bool may_repeat = method == HttpMethod::GET || policy.retry_writes;
	return policy.retry_on_network_error && may_repeat && error.code == ErrorCode::NetworkError;
}

std::chrono::milliseconds retry_delay(std::uint8_t attempt, const RetryPolicy& policy) {
	const std::chrono::milliseconds maximum =
		std::max(policy.max_delay, std::chrono::milliseconds::zero());
	const double max_ms = static_cast<double>(maximum.count());
	double delay_ms = static_cast<double>(std::max(policy.initial_delay.count(), std::int64_t{0}));
	const double multiplier =
		std::isfinite(policy.backoff_multiplier) ? std::max(policy.backoff_multiplier, 0.0) : 1.0;
	for (std::uint8_t i = 1; i < attempt; ++i) {
		delay_ms *= multiplier;
	}
	const double spread =
		std::isfinite(policy.jitter_factor) ? std::clamp(policy.jitter_factor, 0.0, 1.0) : 0.0;
	if (spread > 0.0) {
		static thread_local std::mt19937 rng{std::random_device{}()};
		std::uniform_real_distribution<double> jitter(1.0 - spread, 1.0 + spread);
		delay_ms *= jitter(rng);
	}
	// Comparing before conversion also handles overflow and rounded INT64_MAX.
	if (!std::isfinite(delay_ms) || delay_ms >= max_ms) {
		return maximum;
	}
	return std::chrono::milliseconds{static_cast<std::int64_t>(delay_ms)};
}

RetryingTransport::RetryingTransport(std::shared_ptr<const HttpTransport> inner, RetryPolicy policy)
	: inner_(std::move(inner)), policy_(policy) {}

Result<HttpResponse> RetryingTransport::request(HttpMethod method, std::string_view path,
												std::string_view body) const {
	if (!inner_) {
		return std::unexpected(Error::network("RetryingTransport has no inner transport"));
	}
	const std::uint8_t attempts = std::max<std::uint8_t>(policy_.max_attempts, 1);
	for (std::uint8_t attempt = 1;; ++attempt) {
		Result<HttpResponse> result = inner_->request(method, path, body);
		const bool retry = result ? should_retry(method, *result, policy_)
								  : should_retry(method, result.error(), policy_);
		if (!retry || attempt >= attempts) {
			return result;
		}
		std::chrono::milliseconds delay = retry_delay(attempt, policy_);
		if (result) {
			if (const std::optional<std::chrono::milliseconds> server = retry_after(*result)) {
				delay = std::min(std::max(delay, *server),
								 std::max(policy_.max_delay, std::chrono::milliseconds::zero()));
			}
		}
		std::this_thread::sleep_for(delay);
	}
}

} // namespace kalshi
