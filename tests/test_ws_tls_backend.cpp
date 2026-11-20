// The libwebsockets TLS backend check in src/ws/lws_tls_backend.hpp. The trust
// store code calls OpenSSL on the SSL_CTX libwebsockets hands it, which only an
// OpenSSL build of libwebsockets may reach. This file does not include
// <libwebsockets.h>, so it can set lws_config.h's macros itself.

#include <gtest/gtest.h>

namespace {

#include "lws_tls_backend.hpp"
constexpr bool kWithoutTls = KALSHI_LWS_TLS_IS_OPENSSL == 1;

#define LWS_WITH_TLS
#include "lws_tls_backend.hpp"
constexpr bool kOpenSsl = KALSHI_LWS_TLS_IS_OPENSSL == 1;

// LWS_OPENSSL_SUPPORT is set for every TLS backend, so it proves nothing.
#define LWS_OPENSSL_SUPPORT
#include "lws_tls_backend.hpp"
constexpr bool kOpenSslWithLegacyMacro = KALSHI_LWS_TLS_IS_OPENSSL == 1;

#define LWS_WITH_MBEDTLS
#include "lws_tls_backend.hpp"
constexpr bool kMbedTls = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_MBEDTLS

#define LWS_WITH_POLARSSL
#include "lws_tls_backend.hpp"
constexpr bool kPolarSsl = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_POLARSSL

#define LWS_WITH_BORINGSSL
#include "lws_tls_backend.hpp"
constexpr bool kBoringSsl = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_BORINGSSL

#define LWS_WITH_AWSLC
#include "lws_tls_backend.hpp"
constexpr bool kAwsLc = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_AWSLC

#define LWS_WITH_WOLFSSL
#include "lws_tls_backend.hpp"
constexpr bool kWolfSslOption = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_WOLFSSL

#define USE_WOLFSSL
#include "lws_tls_backend.hpp"
constexpr bool kWolfSsl = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef USE_WOLFSSL

#define USE_OLD_CYASSL
#include "lws_tls_backend.hpp"
constexpr bool kCyaSsl = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef USE_OLD_CYASSL

#define LWS_WITH_BEARSSL
#include "lws_tls_backend.hpp"
constexpr bool kBearSsl = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_BEARSSL

#define LWS_WITH_SCHANNEL
#include "lws_tls_backend.hpp"
constexpr bool kSchannel = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_SCHANNEL

#define LWS_WITH_GNUTLS
#include "lws_tls_backend.hpp"
constexpr bool kGnuTls = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_GNUTLS

#define LWS_WITH_OPENHITLS
#include "lws_tls_backend.hpp"
constexpr bool kOpenHiTls = KALSHI_LWS_TLS_IS_OPENSSL == 1;
#undef LWS_WITH_OPENHITLS

} // namespace

TEST(WsTlsBackend, OnlyAnOpenSslBuildOfLibwebsocketsCountsAsOpenSsl) {
	EXPECT_FALSE(kWithoutTls);
	EXPECT_TRUE(kOpenSsl);
	EXPECT_TRUE(kOpenSslWithLegacyMacro);
	EXPECT_FALSE(kMbedTls);
	EXPECT_FALSE(kPolarSsl);
	EXPECT_FALSE(kBoringSsl);
	EXPECT_FALSE(kAwsLc);
	EXPECT_FALSE(kWolfSslOption);
	EXPECT_FALSE(kWolfSsl);
	EXPECT_FALSE(kCyaSsl);
	EXPECT_FALSE(kBearSsl);
	EXPECT_FALSE(kSchannel);
	EXPECT_FALSE(kGnuTls);
	EXPECT_FALSE(kOpenHiTls);
}
