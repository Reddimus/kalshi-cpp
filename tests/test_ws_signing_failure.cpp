// A reconnect attempt whose handshake cannot be signed must be reported and
// retried like any other failed attempt.
//
// Signing a request only fails once a key has loaded if OpenSSL itself fails,
// so this makes OpenSSL's allocator fail on demand. OpenSSL takes a
// replacement allocator only before its first allocation, which is why the
// hook is installed while this binary's globals are initialized.

#include "kalshi/websocket.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <mutex>
#include <openssl/crypto.h>
#include <string>
#include <string_view>
#include <vector>

#include "support/ws_test_server.hpp"
#include "test_signer_fixture.hpp"

namespace {

using namespace std::chrono_literals;

std::atomic<bool> openssl_out_of_memory{false};

void* openssl_malloc(std::size_t size, const char* /*file*/, int /*line*/) {
	return openssl_out_of_memory ? nullptr : std::malloc(size);
}

void* openssl_realloc(void* block, std::size_t size, const char* /*file*/, int /*line*/) {
	return openssl_out_of_memory ? nullptr : std::realloc(block, size);
}

void openssl_free(void* block, const char* /*file*/, int /*line*/) {
	std::free(block);
}

const bool allocator_installed =
	CRYPTO_set_mem_functions(openssl_malloc, openssl_realloc, openssl_free) == 1;

/// Collects the errors a client reports so the test thread can wait for one.
class Errors {
public:
	void record(const kalshi::WsError& error) {
		const std::lock_guard lock(mutex_);
		messages_.push_back(error.message);
		changed_.notify_all();
	}

	/// Waits for an error whose message contains `text`.
	bool saw(std::string_view text, std::chrono::milliseconds timeout) {
		std::unique_lock lock(mutex_);
		return changed_.wait_for(lock, timeout, [&] {
			return std::any_of(messages_.begin(), messages_.end(),
							   [text](const std::string& message) {
								   return message.find(text) != std::string::npos;
							   });
		});
	}

private:
	std::mutex mutex_;
	std::condition_variable changed_;
	std::vector<std::string> messages_;
};

TEST(WsSigningFailure, AReconnectThatCannotSignIsReportedAndRetried) {
	if (!allocator_installed) {
		GTEST_SKIP() << "OpenSSL had already allocated memory, so it kept its own allocator";
	}
	kalshi::test::WsTestServer server;
	if (!server.running()) {
		GTEST_SKIP() << "could not start a local WebSocket server";
	}
	kalshi::WsConfig config;
	config.url = server.url();
	config.connect_timeout = 5s;
	config.reconnect_delay = 20ms;
	config.max_reconnect_delay = 50ms;
	Errors errors;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), config);
	ws.on_error([&errors](const kalshi::WsError& error) { errors.record(error); });
	ASSERT_TRUE(ws.connect().has_value());
	ASSERT_TRUE(server.wait_for_connections(1));

	// An ordinary failed attempt first: the server refuses the handshake.
	server.reject(true);
	server.drop();
	ASSERT_TRUE(errors.saw("Reconnect failed", 5s));

	// Then an attempt that cannot even be signed.
	openssl_out_of_memory = true;
	const bool reported = errors.saw("signing", 3s);
	openssl_out_of_memory = false;
	EXPECT_TRUE(reported) << "the attempt that could not be signed was never reported";

	// The client must still be trying once signing works again.
	server.reject(false);
	EXPECT_TRUE(server.wait_for_connections(2, 5s)) << "the client stopped reconnecting";
	ws.disconnect();
}

} // namespace
