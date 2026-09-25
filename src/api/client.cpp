#include "kalshi/api.hpp"

#include "support.hpp"

namespace kalshi {

KalshiClient::KalshiClient(HttpClient client)
	: impl_(std::make_unique<Impl>(Impl{std::make_shared<const HttpClient>(std::move(client))})) {}

KalshiClient::KalshiClient(std::shared_ptr<const HttpTransport> transport)
	: impl_(std::make_unique<Impl>(Impl{std::move(transport)})) {}

KalshiClient::~KalshiClient() = default;
KalshiClient::KalshiClient(KalshiClient&&) noexcept = default;
KalshiClient& KalshiClient::operator=(KalshiClient&&) noexcept = default;

const HttpTransport& KalshiClient::transport() const noexcept {
	return *impl_->transport;
}

RateLimitConfig rate_limit_config(const GetAccountApiLimitsResponse& limits,
								  const GetAccountEndpointCostsResponse& costs) {
	RateLimitConfig config;
	// Keep the Basic-tier defaults for any bucket the response did not describe.
	if (limits.read.bucket_capacity > 0 && limits.read.refill_rate > 0) {
		config.read = {.capacity = static_cast<double>(limits.read.bucket_capacity),
					   .refill_per_second = static_cast<double>(limits.read.refill_rate),
					   .initial_tokens = std::nullopt};
	}
	if (limits.write.bucket_capacity > 0 && limits.write.refill_rate > 0) {
		config.write = {.capacity = static_cast<double>(limits.write.bucket_capacity),
						.refill_per_second = static_cast<double>(limits.write.refill_rate),
						.initial_tokens = std::nullopt};
	}
	if (costs.default_cost > 0) {
		config.default_cost = static_cast<double>(costs.default_cost);
	}
	for (const EndpointTokenCost& cost : costs.endpoint_costs) {
		EndpointCostRule rule;
		if (cost.method == "GET") {
			rule.method = HttpMethod::GET;
		} else if (cost.method == "POST") {
			rule.method = HttpMethod::POST;
		} else if (cost.method == "PUT") {
			rule.method = HttpMethod::PUT;
		} else if (cost.method == "DELETE") {
			rule.method = HttpMethod::DEL;
		} else {
			continue;
		}
		rule.path = cost.path;
		rule.cost = static_cast<double>(cost.cost);
		config.costs.push_back(std::move(rule));
	}
	return config;
}

} // namespace kalshi
