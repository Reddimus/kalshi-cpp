// Allocation budgets for hot paths. Each test compares two calls that should
// allocate the same, so it holds on every standard library.

#include "kalshi/rate_limit.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

#include "support/allocation_counter.hpp"

namespace {

using kalshi::test::AllocationProbe;

std::string batch_of(int orders, std::string_view order) {
	std::string body = R"({"orders":[)";
	for (int i = 0; i < orders; ++i) {
		body += i == 0 ? "" : ",";
		body += order;
	}
	body += "]}";
	return body;
}

std::uint64_t cost_allocations(const kalshi::RateLimitedTransport& transport,
							   const std::string& body) {
	const AllocationProbe probe;
	(void)transport.cost(kalshi::HttpMethod::POST, "/portfolio/events/orders/batched", body);
	return probe.count().count;
}

TEST(AllocationBudget, CountingABatchDoesNotCopyItsOrders) {
	const kalshi::RateLimitedTransport transport(nullptr, kalshi::RateLimitConfig{});
	const std::string empty_orders = batch_of(20, "{}");
	const std::string real_orders =
		batch_of(20, R"({"ticker":"KXHIGHNY-26SEP25-T75","side":"yes","action":"buy","count":10,)"
					 R"("yes_price":42,"client_order_id":"5f0c7c1e-8d5a-4f4e-9b1a-2f6d3c9e7a10"})");

	EXPECT_EQ(cost_allocations(transport, real_orders), cost_allocations(transport, empty_orders));
}

} // namespace
