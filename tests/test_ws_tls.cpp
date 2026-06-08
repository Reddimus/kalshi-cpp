#include <gtest/gtest.h>
#include <libwebsockets.h>

#include "ws_ssl_flags.hpp"

namespace {

TEST(WsSslFlags, DefaultVerifyEnablesSslAndForbidsPermissiveFlags) {
	const int flags = kalshi::ws_detail::compute_ws_ssl_flags(/*verify_ssl=*/true);

	EXPECT_NE(flags & LCCSCF_USE_SSL, 0);
	EXPECT_EQ(flags & LCCSCF_ALLOW_SELFSIGNED, 0);
	EXPECT_EQ(flags & LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK, 0);
}

TEST(WsSslFlags, OptOutEnablesAllPermissiveFlags) {
	const int flags = kalshi::ws_detail::compute_ws_ssl_flags(/*verify_ssl=*/false);

	EXPECT_NE(flags & LCCSCF_USE_SSL, 0);
	EXPECT_NE(flags & LCCSCF_ALLOW_SELFSIGNED, 0);
	EXPECT_NE(flags & LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK, 0);
}

} // namespace
