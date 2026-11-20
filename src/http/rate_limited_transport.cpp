#include "kalshi/rate_limit.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "../json.hpp"

namespace kalshi {

namespace http_detail {

// Glaze reflection needs a type with linkage, so this is not in an unnamed namespace.
struct BatchBody {
	std::vector<glz::skip> orders; // Only the count matters, so nothing is copied.
};

} // namespace http_detail

namespace {

/// Items in a batch request body, or 1 for any other body.
std::size_t batch_items(std::string_view path, std::string_view body) {
	if (!path.ends_with("/batched") || body.empty()) {
		return 1;
	}
	http_detail::BatchBody batch;
	// `body` is a view, so no terminator is guaranteed after it.
	constexpr glz::opts options{.null_terminated = false, .error_on_unknown_keys = false};
	if (glz::read<options>(batch, body) || batch.orders.empty()) {
		return 1;
	}
	return batch.orders.size();
}

// Prefix of `text` up to `delimiter`. Built from the pointer so it cannot throw.
std::string_view until(std::string_view text, char delimiter) noexcept {
	const std::size_t end = text.find(delimiter);
	return end == std::string_view::npos ? text : std::string_view{text.data(), end};
}

std::string_view strip_query(std::string_view path) noexcept {
	path = until(path, '?');
	constexpr std::string_view prefix = "/trade-api/v2";
	if (path.starts_with(prefix) && (path.size() == prefix.size() || path[prefix.size()] == '/')) {
		path.remove_prefix(prefix.size());
	}
	return path;
}

/// Matches `/a/{id}/b` against `/a/123/b`; a leading `/trade-api/v2` on the
/// rule is ignored.
bool path_matches(std::string_view rule, std::string_view path) noexcept {
	constexpr std::string_view prefix = "/trade-api/v2";
	if (rule.starts_with(prefix)) {
		rule.remove_prefix(prefix.size());
	}
	while (!rule.empty() || !path.empty()) {
		if (rule.empty() || path.empty() || rule.front() != '/' || path.front() != '/') {
			return false;
		}
		rule.remove_prefix(1);
		path.remove_prefix(1);
		const std::string_view rule_segment = until(rule, '/');
		const std::string_view path_segment = until(path, '/');
		const bool wildcard = rule_segment.starts_with('{') && rule_segment.ends_with('}');
		if (!wildcard && rule_segment != path_segment) {
			return false;
		}
		rule.remove_prefix(rule_segment.size());
		path.remove_prefix(path_segment.size());
	}
	return true;
}

} // namespace

RateLimitedTransport::RateLimitedTransport(std::shared_ptr<const HttpTransport> inner,
										   RateLimitConfig config)
	: inner_(std::move(inner)), config_(std::move(config)), read_(config_.read),
	  write_(config_.write), balance_(config_.balance), transfer_(config_.transfer) {}

bool RateLimitedTransport::is_write(HttpMethod method, std::string_view path) noexcept {
	if (method == HttpMethod::GET) {
		return false;
	}
	path = strip_query(path);
	// Kalshi assigns buckets per operation; see its rate limits guide. Proposing,
	// rejecting, and canceling a block trade use Read.
	constexpr std::array<std::string_view, 6> write_prefixes{
		"/portfolio/events/orders", "/portfolio/orders",		   "/portfolio/order_groups",
		"/communications/rfqs",		"/communications/rfq_batches", "/communications/quotes",
	};
	for (const std::string_view prefix : write_prefixes) {
		if (path.starts_with(prefix) &&
			(path.size() == prefix.size() || path[prefix.size()] == '/')) {
			return true;
		}
	}
	constexpr std::array<std::string_view, 6> write_routes{
		"/portfolio/positions/{ticker}/exit_trigger",
		"/communications/block-trade-proposals/{block_trade_proposal_id}/accept",
		"/weather_point_markets",
		"/concert_cancellation_markets",
		"/portfolio/target_balance_allocation",
		"/account/api_usage_level/upgrade",
	};
	return std::any_of(write_routes.begin(), write_routes.end(),
					   [path](std::string_view route) { return path_matches(route, path); });
}

double RateLimitedTransport::cost(HttpMethod method, std::string_view path,
								  std::string_view body) const {
	const std::string_view route = strip_query(path);
	if (method == HttpMethod::GET && route == "/portfolio/balance") {
		return 1.0;
	}
	double unit_cost = config_.default_cost;
	for (const EndpointCostRule& rule : config_.costs) {
		if (rule.method == method && path_matches(rule.path, route)) {
			unit_cost = rule.cost;
			break;
		}
	}
	return unit_cost * static_cast<double>(batch_items(route, body));
}

Result<HttpResponse> RateLimitedTransport::request(HttpMethod method, std::string_view path,
												   std::string_view body) const {
	if (!inner_) {
		return std::unexpected(Error::network("RateLimitedTransport has no inner transport"));
	}
	const std::string_view route = strip_query(path);
	TokenBucket& bucket =
		method == HttpMethod::GET && route == "/portfolio/balance" ? balance_
		: method == HttpMethod::POST && route == "/portfolio/intra_exchange_instance_transfer"
			? transfer_
			: (is_write(method, path) ? write_ : read_);
	const double tokens = cost(method, path, body);
	if (!std::isfinite(tokens) || tokens < 0.0) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "Token costs must be finite and non-negative"});
	}
	if (tokens > bucket.config().capacity) {
		return std::unexpected(Error{
			ErrorCode::InvalidRequest,
			"Request costs " + std::to_string(tokens) + " tokens but the rate-limit bucket holds " +
				std::to_string(bucket.config().capacity) + "; split the batch"});
	}
	const std::chrono::nanoseconds max_wait =
		config_.max_wait >= std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::nanoseconds::max())
			? std::chrono::nanoseconds::max()
			: std::chrono::duration_cast<std::chrono::nanoseconds>(
				  std::max(config_.max_wait, std::chrono::milliseconds::zero()));
	if (!bucket.acquire_for(tokens, max_wait)) {
		// No server answered, so http_status stays 0.
		return std::unexpected(Error{
			ErrorCode::RateLimited, "Rate-limit tokens would not refill within max_wait", 0, {}});
	}
	return inner_->request(method, path, body);
}

} // namespace kalshi
