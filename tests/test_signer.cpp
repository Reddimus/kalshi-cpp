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

/// Verify a signature the way Kalshi's gateway does: RSA-PSS/SHA-256 or Ed25519.
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
	const bool rsa = EVP_PKEY_get_base_id(key) == EVP_PKEY_RSA;
	const bool verified =
		ctx &&
		EVP_DigestVerifyInit(ctx, &pkey_ctx, rsa ? EVP_sha256() : nullptr, nullptr, key) == 1 &&
		(!rsa || (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) == 1 &&
				  EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) == 1)) &&
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

TEST(Signer, Ed25519KeysSignWithEd25519) {
	const std::string& pem = kalshi::test::ed25519_private_key_pem();
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("ed-key", pem);
	ASSERT_TRUE(signer.has_value()) << signer.error().message;
	EXPECT_EQ(signer->key_type(), kalshi::KeyType::Ed25519);

	const std::string path = "/trade-api/v2/portfolio/balance";
	const kalshi::Result<kalshi::AuthHeaders> headers =
		signer->sign_with_timestamp("GET", path, 1234567890000);
	ASSERT_TRUE(headers.has_value()) << headers.error().message;
	EXPECT_EQ(headers->signature.size(), 88U); // 64 bytes, base64
	EXPECT_TRUE(signature_verifies(pem, "1234567890000GET" + path, headers->signature));
	EXPECT_FALSE(signature_verifies(pem, "1234567890000GET/other", headers->signature));
}

TEST(Signer, RsaKeysReportTheirType) {
	EXPECT_EQ(kalshi::test::make_signer().key_type(), kalshi::KeyType::Rsa);
}

TEST(Signer, CopiesShareTheKeyAndSignIndependently) {
	const kalshi::Signer original = kalshi::test::make_signer("shared");
	const kalshi::Signer copy = original; // NOLINT(performance-unnecessary-copy-initialization)
	EXPECT_EQ(copy.api_key_id(), "shared");
	EXPECT_TRUE(copy.sign("GET", "/trade-api/v2/markets").has_value());
	EXPECT_TRUE(original.sign("GET", "/trade-api/v2/markets").has_value());
}

TEST(Signer, EncryptedKeysFailWithoutPrompting) {
	const std::string pem = kalshi::test::generate_pem("RSA", "passphrase");
	ASSERT_NE(pem.find("ENCRYPTED"), std::string::npos);
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("key", pem);
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::SigningError);
}

TEST(Signer, UnsupportedKeyTypesFailAtLoadTime) {
	const kalshi::Result<kalshi::Signer> signer =
		kalshi::Signer::from_pem("key", kalshi::test::generate_pem("EC"));
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
}

TEST(Signer, MissingKeyFileNamesThePath) {
	const kalshi::Result<kalshi::Signer> signer =
		kalshi::Signer::from_pem_file("key", "/nonexistent/kalshi.pem");
	ASSERT_FALSE(signer.has_value());
	EXPECT_NE(signer.error().message.find("/nonexistent/kalshi.pem"), std::string::npos);
}
