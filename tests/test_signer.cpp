#include "kalshi/signer.hpp"

#include <gtest/gtest.h>
#include <string>

// Throwaway 2048-bit RSA private key for tests only. Generated via
// ``openssl genrsa -traditional 2048``. Never used to sign live
// traffic.
//
// Replaces a previous hand-crafted PEM whose private-key components
// were structured nonsense (only the public part decoded as base64).
// OpenSSL's ``d2i_RSAPrivateKey`` rejected it, so
// ``Signer::SignProducesHeaders`` was permanently ``GTEST_SKIP``'d
// — the signing path therefore had **no test coverage** and a
// regression in ``Signer::sign()`` could ship without surfacing.
const char* TEST_RSA_KEY = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA3ZNc4DAP9V2Rq8H1zNNb2X7x7hxQrCtWt6D7EnuFXsOLMJ0B
2WPDUVlvVlsF5EofNM0Tejf3BP6x1qtPxY+LC1oGcvVPJhQp3AiCRjt1fpZeEqaR
H9/pSatURcb+E/M4kL4F4IK1vn6RnqA/VqO2WQiKCMfQgL9h+XCNi12wX9l+sH+8
6mIX2yan0UeKGKvLBgNjdqxaD9QElo0lz+APvNF9bTSHe4zSycr8MQvWVmY+/CG9
6JzIid/SFChYJjZ5btDRZy8/iHftf99lo1pJ80I/NKiHk/wlsiQpJpAVmBYBfOxp
4YS3Uy3uEnWvTvbGl1zT4aZAa1jOmGGvZiHUUQIDAQABAoIBAC7/w09NepEX9h55
36bA/WZawjv41xbSAYylUaRXvZA+h5956kq/mc4/W3m0iIEmRMzJJDTEOrodQUEw
6NSV0E9J2vzW8mE4HTHuPx3hHljJ0e4AVV+uudf1xsQfQ8UdDfZLzEjVSPI9fCtq
v8yjoLntcQQQSDaLAd/sYyW464DE50pUwgt3wGyIHx19m+TF8ntULzZv/e0P++hW
ox8L1Ytfd5h1augQ0K3rD27i9QSOauesLN4cc2eZZ8ow1rr6pR38++NHyrYO/NaR
nm6qU6W0EqUgyZdRaApD89UK1lGvt9rq++Yg2kAtw3MPsFoK2lFTYjEyGphyadBN
dG9jym0CgYEA8PF7m6C+LcPdKk1BoN2b3mt9FvV0AJu/3yIijHd2RCSmr2OVV2py
SZ0J9xWQi95eSU0y5RUiA3k82AFHlPJSsCpLqfeAS8fJcYHo09mMM6+xKv8E6KEW
Laxt6GsIeaJRLkcn4pO2ZAq4aTk79lhNHXc7rg0kKNokgvMBamPIN60CgYEA62wL
MKpGfUp6Hl1jOszbXt867XbTSX2eoK1HQ8mkeGlxJgBI7UrIKIaArbU1Ov/gAfu5
gDWiLcxZtl8gHRjlEaSl01wKM6AXbEl+3jU1lOATscdsRyeyJ48dk5IWPZygnVUo
+amtQyet07Bht90Q3ULsDPaFXVtZAJDxj0vHM7UCgYEAlsg4d6M3gMJjBNcGLBqj
MaUIyjZfGwZdI9Fj143nGCvrmDT0v5jg3sqE8viu1akaTjsej5gTCiNz/SWH22Fu
d8pwQXSe+E2V9g+7WeB5ydq4P9UKCF7O11RiD6Hz0tLOhOyIvFV+PcsrrsXfjYGi
+L6mPX0B1QL2+HAEwcSiBp0CgYA82hSaY6kMwa+HIcSAcmtRvonQz6IVoO7bwW5m
SzzEEx04IWK4U1ghgYLJY8l6kqEoYhS02ygshmG6DiSS4Nh1EwX5+BR6+6qSRv0Q
GtjavoDYtx951Pzr1MZkWqJ9EntBr72DqyQp85uu2CyqBe5SAvZY82/NjcsXpl+K
FqBK8QKBgQDeSgsq3ljKeIuLBl9yYzc9rEGnMZJl33wcVgAG6jTr+qZ5iEToiRTu
TSBYZgwgMJTq7i4blpsYJCoHcQynRJgSmIvSLuCw/ovyBHmHvJDDjrFsCutQYXA9
Y2lRUoypd9DhH64Hki9kaqKd23817XEXR9kwu7QdrzWYWST5l+cTAg==
-----END RSA PRIVATE KEY-----
)";

TEST(Signer, FromInvalidPemFails) {
	kalshi::Result<kalshi::Signer> result =
		kalshi::Signer::from_pem("test_key_id", "invalid pem data");
	ASSERT_FALSE(result.has_value());
}

TEST(Signer, ApiKeyIdStored) {
	kalshi::Result<kalshi::Signer> result = kalshi::Signer::from_pem("my_api_key", TEST_RSA_KEY);
	ASSERT_TRUE(result.has_value()) << "TEST_RSA_KEY failed to parse — regenerate via "
									   "openssl genrsa -traditional 2048";
	ASSERT_EQ(std::string(result->api_key_id()), std::string("my_api_key"));
}

TEST(Signer, SignProducesHeaders) {
	kalshi::Result<kalshi::Signer> signer_result =
		kalshi::Signer::from_pem("test_key", TEST_RSA_KEY);
	ASSERT_TRUE(signer_result.has_value()) << "TEST_RSA_KEY failed to parse — regenerate via "
											  "openssl genrsa -traditional 2048";

	kalshi::Result<kalshi::AuthHeaders> headers_result =
		signer_result->sign_with_timestamp("GET", "/trade-api/v2/markets", 1234567890000);
	ASSERT_TRUE(headers_result.has_value()) << "Failed to sign: " << headers_result.error().message;

	const kalshi::AuthHeaders& headers = *headers_result;
	ASSERT_EQ(headers.access_key, std::string("test_key"));
	ASSERT_EQ(headers.timestamp, std::string("1234567890000"));
	ASSERT_FALSE(headers.signature.empty());
}
