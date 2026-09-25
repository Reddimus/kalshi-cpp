#include "kalshi/rate_limit.hpp"

#include <array>

#include "../json.hpp"

namespace kalshi {

namespace http_detail {

// Glaze reflection needs a type with linkage, so this is not in an unnamed namespace.
struct BatchBody {
	std::vector<glz::raw_json> orders;
};

} // namespace http_detail

namespace {

/// Items in a batch request body, or 1 for any other body.
std::size_t batch_items(std::string_view path, std::string_view body) {
	if (!path.ends_with("/batched") || body.empty()) {
		return 1;
	}
	http_detail::BatchBody batch;
	constexpr glz::opts options{.error_on_unknown_keys = false};
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
	return until(path, '?');
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
	  write_(config_.write) {}

bool RateLimitedTransport::is_write(HttpMethod method, std::string_view path) noexcept {
	if (method == HttpMethod::GET) {
		return false;
	}
	path = strip_query(path);
	constexpr std::array<std::string_view, 6> write_prefixes{
		"/portfolio/events/orders", "/portfolio/orders",
		"/portfolio/order_groups",	"/communications/rfqs",
		"/communications/quotes",	"/communications/block-trade-proposals",
	};
	for (const std::string_view prefix : write_prefixes) {
		if (path.starts_with(prefix)) {
			return true;
		}
	}
	return false;
}

double RateLimitedTransport::cost(HttpMethod method, std::string_view path,
								  std::string_view body) const {
	const std::string_view route = strip_query(path);
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
	TokenBucket& bucket = is_write(method, path) ? write_ : read_;
	if (!bucket.acquire_for(cost(method, path, body), config_.max_wait)) {
		return std::unexpected(Error{ErrorCode::RateLimited,
									 "Rate limit budget would not refill within max_wait", 429});
	}
	return inner_->request(method, path, body);
}

} // namespace kalshi
