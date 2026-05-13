#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/signer.hpp"

#define private public
#include "kalshi/api.hpp"
#undef private

#include <gtest/gtest.h>
#include <string>
#include <utility>

extern const char* TEST_RSA_KEY;

namespace {

kalshi::KalshiClient make_client() {
	auto signer = kalshi::Signer::from_pem("test_key", TEST_RSA_KEY);
	EXPECT_TRUE(signer.has_value()) << "test RSA key failed to parse";
	kalshi::HttpClient http_client(std::move(*signer));
	return kalshi::KalshiClient(std::move(http_client));
}

} // namespace

TEST(QueryBuilders, SeriesListEncodesStringParameters) {
	kalshi::KalshiClient client = make_client();

	kalshi::GetSeriesParams params;
	params.limit = 200;
	params.category = "Climate and Weather";
	params.cursor = "abc+/=";

	EXPECT_EQ(client.build_series_query(params),
			  "/series?limit=200&cursor=abc%2B%2F%3D&category=Climate%20and%20Weather");
}
