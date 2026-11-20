// No include guard: tests include this again after changing the macros below.
//
// Sets KALSHI_LWS_TLS_IS_OPENSSL to 1 when libwebsockets was built with TLS on
// OpenSSL, and to 0 otherwise. Include <libwebsockets.h>, which includes
// lws_config.h, first.
//
// Only with OpenSSL is the SSL_CTX that libwebsockets hands its callbacks an
// object this SDK's OpenSSL calls can use; any other backend's context would
// be corrupted by them. libwebsockets has no macro that names OpenSSL itself:
// LWS_WITH_TLS and the deprecated LWS_OPENSSL_SUPPORT are also set for mbedTLS
// and BearSSL. So this lists every other backend that lws_config.h can name,
// as of libwebsockets 4.3 through 5.1. BoringSSL, AWS-LC, and wolfSSL offer
// OpenSSL's API, but an SSL_CTX of theirs is not one of OpenSSL's either.
// Check cmake/lws_config.h.in for new backends on every libwebsockets upgrade.

#undef KALSHI_LWS_TLS_IS_OPENSSL
#if defined(LWS_WITH_TLS) && !defined(LWS_WITH_MBEDTLS) && !defined(LWS_WITH_POLARSSL) &&     \
	!defined(LWS_WITH_BORINGSSL) && !defined(LWS_WITH_AWSLC) && !defined(LWS_WITH_WOLFSSL) && \
	!defined(USE_WOLFSSL) && !defined(USE_OLD_CYASSL) && !defined(LWS_WITH_BEARSSL) &&        \
	!defined(LWS_WITH_SCHANNEL) && !defined(LWS_WITH_GNUTLS) && !defined(LWS_WITH_OPENHITLS)
#define KALSHI_LWS_TLS_IS_OPENSSL 1
#else
#define KALSHI_LWS_TLS_IS_OPENSSL 0
#endif
