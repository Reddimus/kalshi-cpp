#pragma once

#include <libwebsockets.h>

namespace kalshi::ws_detail {

/// Builds the libwebsockets SSL flag mask for a WSS connection.
[[nodiscard]] inline int compute_ws_ssl_flags(bool verify_ssl) noexcept {
	if (verify_ssl) {
		return LCCSCF_USE_SSL;
	}
	return LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
}

} // namespace kalshi::ws_detail
