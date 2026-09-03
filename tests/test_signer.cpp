#include "kalshi/detail/http_path.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/signer.hpp"

#include <gtest/gtest.h>
#include <string>

#include "test_signer_fixture.hpp"

TEST(Signer, FromInvalidPemFails) {
	kalshi::Result<kalshi::Signer> result =
		kalshi::Signer::from_pem("test_key_id", "invalid pem data");
	ASSERT_FALSE(result.has_value());
}

TEST(Signer, ApiKeyIdStored) {
	kalshi::Result<kalshi::Signer> result =
		kalshi::Signer::from_pem("my_api_key", kalshi::test::private_key_pem());
	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(std::string(result->api_key_id()), std::string("my_api_key"));
}

TEST(Signer, SignProducesHeaders) {
	kalshi::Result<kalshi::Signer> signer_result =
		kalshi::Signer::from_pem("test_key", kalshi::test::private_key_pem());
	ASSERT_TRUE(signer_result.has_value());

	kalshi::Result<kalshi::AuthHeaders> headers_result =
		signer_result->sign_with_timestamp("GET", "/trade-api/v2/markets", 1234567890000);
	ASSERT_TRUE(headers_result.has_value()) << "Failed to sign: " << headers_result.error().message;

	const kalshi::AuthHeaders& headers = *headers_result;
	ASSERT_EQ(headers.access_key, std::string("test_key"));
	ASSERT_EQ(headers.timestamp, std::string("1234567890000"));
	ASSERT_FALSE(headers.signature.empty());
}

TEST(Signer, MovedFromInstanceRemainsSafe) {
	kalshi::Result<kalshi::Signer> signer_result =
		kalshi::Signer::from_pem("test_key", kalshi::test::private_key_pem());
	ASSERT_TRUE(signer_result.has_value());
	kalshi::Signer signer = std::move(*signer_result);
	kalshi::Signer moved_to = std::move(signer);

	EXPECT_TRUE(signer.api_key_id().empty());
	const kalshi::Result<kalshi::AuthHeaders> headers =
		signer.sign_with_timestamp("GET", "/trade-api/v2/markets", 1234567890000);
	ASSERT_FALSE(headers.has_value());
	EXPECT_EQ(headers.error().code, kalshi::ErrorCode::SigningError);
	EXPECT_EQ(moved_to.api_key_id(), "test_key");
}

TEST(HttpClient, MovedFromInstanceRemainsSafe) {
	kalshi::Result<kalshi::Signer> signer_result =
		kalshi::Signer::from_pem("test_key", kalshi::test::private_key_pem());
	ASSERT_TRUE(signer_result.has_value());
	kalshi::HttpClient client(std::move(*signer_result));
	kalshi::HttpClient moved_to(std::move(client));

	EXPECT_NO_THROW((void)client.config());
	const kalshi::Result<kalshi::HttpResponse> response = client.get("/markets");
	ASSERT_FALSE(response.has_value());
	EXPECT_EQ(response.error().code, kalshi::ErrorCode::NetworkError);
	EXPECT_FALSE(moved_to.config().base_url.empty());
}

TEST(HttpSigningPath, UsesFullApiPathAndOmitsQuery) {
	EXPECT_EQ(kalshi::detail::request_signing_path("https://external-api.kalshi.com/trade-api/v2",
												   "/portfolio/orders?limit=5"),
			  "/trade-api/v2/portfolio/orders");
	EXPECT_EQ(kalshi::detail::request_signing_path("https://example.test/custom/root/",
												   "portfolio/balance?subaccount=7"),
			  "/custom/root/portfolio/balance");
}

TEST(HttpSigningPath, RequestUrlUsesTheSameNormalizedJoin) {
	EXPECT_EQ(kalshi::detail::request_url("https://example.test/trade-api/v2/",
										  "/portfolio/orders?limit=5"),
			  "https://example.test/trade-api/v2/portfolio/orders?limit=5");
}
