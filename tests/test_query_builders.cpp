#include <gtest/gtest.h>
#include <string>

#include "query_builders.hpp"

// The query-builder logic is exercised through the free function
// ``kalshi::api_detail::build_series_query_string`` rather than the
// private ``KalshiClient::build_series_query`` member. This avoids
// the ``#define private public`` hack which doesn't link on MSVC —
// on Windows the access modifier is part of the mangled symbol name,
// so a translation unit that re-declares a private method as public
// emits a different symbol than the implementation TU and the linker
// can't find it. Routing through ``api_detail`` keeps the test
// portable to all three CI platforms.

TEST(QueryBuilders, SeriesListEncodesStringParameters) {
	kalshi::GetSeriesParams params;
	params.limit = 200;
	params.category = "Climate and Weather";
	params.cursor = "abc+/=";

	EXPECT_EQ(kalshi::api_detail::build_series_query_string(params),
			  "/series?limit=200&cursor=abc%2B%2F%3D&category=Climate%20and%20Weather");
}

TEST(QueryBuilders, CancelOrderV2AddsOptionalSelectors) {
	kalshi::CancelOrderV2Params params;
	params.order_id = "order-123";
	params.subaccount = 9;
	params.exchange_index = 0;

	EXPECT_EQ(kalshi::api_detail::build_cancel_order_v2_path(params),
			  "/portfolio/events/orders/order-123?subaccount=9&exchange_index=0");
}

TEST(QueryBuilders, CancelOrderV2OmitsUnsetSelectors) {
	kalshi::CancelOrderV2Params params;
	params.order_id = "order-123";

	EXPECT_EQ(kalshi::api_detail::build_cancel_order_v2_path(params),
			  "/portfolio/events/orders/order-123");
}
