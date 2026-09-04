#include "kalshi/detail/http_path.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/signer.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <string>
#include <vector>

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

TEST(HttpSigningPath, WebSocketCallSitePassesAnEmptyBaseUrl) {
	// WebSocketClient::connect() signs `request_signing_path("", endpoint.path)`
	// because the WS URL already carries the full path from the host root.
	// Pin that empty-base behaviour: the path is passed through untouched
	// and any query string is dropped, matching the REST contract above.
	EXPECT_EQ(kalshi::detail::request_signing_path("", "/trade-api/ws/v2"), "/trade-api/ws/v2");
	EXPECT_EQ(kalshi::detail::request_signing_path("", "/trade-api/ws/v2?token=value"),
			  "/trade-api/ws/v2");
	EXPECT_EQ(kalshi::detail::request_signing_path("", "/"), "/");
	EXPECT_EQ(kalshi::detail::request_signing_path("", "/?token=value"), "/");
}

namespace {

/// Verify an RSA-PSS/SHA-256 signature the way Kalshi's gateway does.
///
/// RSA-PSS salts every signature, so two calls over the same input are
/// never byte-identical and cannot be pinned with a golden string. What
/// *is* stable, and what a signing regression would break, is that the
/// emitted signature verifies against the key over exactly
/// `timestamp + method + path`.
bool signature_verifies(const std::string& pem, const std::string& message,
						const std::string& base64_signature) {
	BIO* key_bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
	if (!key_bio)
		return false;
	EVP_PKEY* key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
	BIO_free(key_bio);
	if (!key)
		return false;

	std::vector<unsigned char> signature(base64_signature.size());
	const int decoded_with_padding = EVP_DecodeBlock(
		signature.data(), reinterpret_cast<const unsigned char*>(base64_signature.data()),
		static_cast<int>(base64_signature.size()));
	if (decoded_with_padding < 0) {
		EVP_PKEY_free(key);
		return false;
	}
	std::size_t padding = 0;
	while (padding < base64_signature.size() &&
		   base64_signature[base64_signature.size() - 1 - padding] == '=')
		++padding;
	signature.resize(static_cast<std::size_t>(decoded_with_padding) - padding);

	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	EVP_PKEY_CTX* pkey_ctx = nullptr;
	const bool verified = ctx &&
						  EVP_DigestVerifyInit(ctx, &pkey_ctx, EVP_sha256(), nullptr, key) == 1 &&
						  EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) == 1 &&
						  EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) == 1 &&
						  EVP_DigestVerify(ctx, signature.data(), signature.size(),
										   reinterpret_cast<const unsigned char*>(message.data()),
										   message.size()) == 1;
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(key);
	return verified;
}

} // namespace

TEST(Signer, SignatureVerifiesOverTimestampMethodAndPath) {
	const std::string& pem = kalshi::test::private_key_pem();
	kalshi::Result<kalshi::Signer> signer_result = kalshi::Signer::from_pem("test_key", pem);
	ASSERT_TRUE(signer_result.has_value());

	// The WebSocket handshake signs the endpoint path with no query string.
	const std::string path = "/trade-api/ws/v2";
	const kalshi::Result<kalshi::AuthHeaders> headers =
		signer_result->sign_with_timestamp("GET", path, 1234567890000);
	ASSERT_TRUE(headers.has_value());
	EXPECT_EQ(headers->timestamp, "1234567890000");

	EXPECT_TRUE(signature_verifies(pem, "1234567890000GET" + path, headers->signature));
	// A different path, method, or timestamp must not verify — otherwise
	// the assertion above would pass for any signed message.
	EXPECT_FALSE(
		signature_verifies(pem, "1234567890000GET" + path + "?token=value", headers->signature));
	EXPECT_FALSE(signature_verifies(pem, "1234567890000POST" + path, headers->signature));
	EXPECT_FALSE(signature_verifies(pem, "1234567890001GET" + path, headers->signature));
}
