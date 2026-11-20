// Which certificate authorities WebSocketClient trusts, against a local TLS
// server whose certificate comes from a throwaway authority.

#include "kalshi/websocket.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <libwebsockets.h>
#include <optional>
#include <string>
#include <system_error>

#include "support/scratch_directory.hpp"
#include "support/test_certificates.hpp"
#include "support/ws_test_server.hpp"
#include "test_signer_fixture.hpp"

namespace {

using namespace std::chrono_literals;
using kalshi::test::ScratchDirectory;
using kalshi::test::TestCertificateAuthority;
using kalshi::test::WsTestServer;

/// Changes the process's working directory until the end of the scope.
class WorkingDirectory {
public:
	explicit WorkingDirectory(const std::filesystem::path& path)
		: previous_(std::filesystem::current_path()) {
		std::filesystem::current_path(path);
	}
	~WorkingDirectory() {
		std::error_code ignored;
		std::filesystem::current_path(previous_, ignored);
	}
	WorkingDirectory(const WorkingDirectory&) = delete;
	WorkingDirectory& operator=(const WorkingDirectory&) = delete;

private:
	std::filesystem::path previous_;
};

/// Sets or clears an environment variable until the end of the scope.
class EnvironmentVariable {
public:
	EnvironmentVariable(std::string name, const std::optional<std::string>& value)
		: name_(std::move(name)) {
		if (const char* current = std::getenv(name_.c_str())) {
			previous_ = current;
		}
		set(value);
	}
	~EnvironmentVariable() { set(previous_); }
	EnvironmentVariable(const EnvironmentVariable&) = delete;
	EnvironmentVariable& operator=(const EnvironmentVariable&) = delete;

private:
	void set(const std::optional<std::string>& value) const {
#ifdef _WIN32
		(void)::_putenv_s(name_.c_str(), value ? value->c_str() : "");
#else
		if (value) {
			(void)::setenv(name_.c_str(), value->c_str(), 1);
		} else {
			(void)::unsetenv(name_.c_str());
		}
#endif
	}

	std::string name_;
	std::optional<std::string> previous_;
};

/// A TLS server whose certificate the test's own authority issued.
struct TlsServer {
	explicit TlsServer(const std::string& subject_alt_name)
		: issued(authority.issue(subject_alt_name)),
		  server(WsTestServer::TlsFiles{scratch.write("server.pem", issued.certificate),
										scratch.write("server.key", issued.private_key)}) {}

	[[nodiscard]] kalshi::WsConfig config() const {
		kalshi::WsConfig config;
		config.url = server.url();
		config.connect_timeout = 5s;
		config.auto_reconnect = false;
		return config;
	}

	ScratchDirectory scratch;
	TestCertificateAuthority authority;
	TestCertificateAuthority::Issued issued;
	WsTestServer server;
};

// The server listens on 127.0.0.1. Both forms are listed because TLS libraries
// differ on whether an address may match a DNS name.
constexpr const char* kLoopbackNames = "DNS:127.0.0.1,IP:127.0.0.1";

} // namespace

TEST(WsTls, AnAuthorityInTheWorkingDirectorysShareFolderIsNotTrusted) {
	// libwebsockets adds this directory to the client's trust store when no CA
	// file is configured. It is "../share" unless the library was built
	// otherwise, which is relative to wherever the process happens to run.
	const std::filesystem::path compiled_in{LWS_OPENSSL_CLIENT_CERTS};
	if (compiled_in.is_absolute()) {
		GTEST_SKIP() << "libwebsockets was built with the absolute CA path " << compiled_in;
	}
	TlsServer tls{kLoopbackNames};
	if (!tls.server.running()) {
		GTEST_SKIP() << "could not start a local TLS WebSocket server";
	}
	const std::filesystem::path work = std::filesystem::path{tls.scratch.path()} / "a" / "work";
	std::filesystem::create_directories(work);
	const std::filesystem::path planted =
		(work / compiled_in).lexically_normal() / tls.authority.hashed_file_name();
	std::filesystem::create_directories(planted.parent_path());
	(void)tls.scratch.write(std::filesystem::relative(planted, tls.scratch.path()).generic_string(),
							tls.authority.certificate());
	// Nothing else may vouch for the authority.
	const EnvironmentVariable no_file{"SSL_CERT_FILE", std::nullopt};
	const EnvironmentVariable no_directory{"SSL_CERT_DIR", std::nullopt};
	const WorkingDirectory elsewhere{work};

	kalshi::WebSocketClient ws(kalshi::test::make_signer(), tls.config());
	const kalshi::Result<void> connected = ws.connect();

	ASSERT_FALSE(connected.has_value()) << "trusted a certificate authority found in " << planted;
	EXPECT_EQ(connected.error().code, kalshi::ErrorCode::NetworkError);
	EXPECT_FALSE(ws.is_connected());
}

TEST(WsTls, AuthoritiesInTheSystemsDefaultVerifyPathsAreTrusted) {
	TlsServer tls{kLoopbackNames};
	if (!tls.server.running()) {
		GTEST_SKIP() << "could not start a local TLS WebSocket server";
	}
	// OpenSSL reads its default CA file from here. libcurl does not, so this
	// trusts the authority for WebSocket connections only.
	const EnvironmentVariable trusted{"SSL_CERT_FILE",
									  tls.scratch.write("ca.pem", tls.authority.certificate())};

	kalshi::WebSocketClient ws(kalshi::test::make_signer(), tls.config());
	const kalshi::Result<void> connected = ws.connect();

	ASSERT_TRUE(connected.has_value()) << connected.error().message;
	EXPECT_TRUE(ws.is_connected());
	ws.disconnect();
}

TEST(WsTls, ATrustedCertificateForAnotherHostIsRejected) {
	TlsServer tls{"DNS:another-host.test"};
	if (!tls.server.running()) {
		GTEST_SKIP() << "could not start a local TLS WebSocket server";
	}
	const EnvironmentVariable trusted{"SSL_CERT_FILE",
									  tls.scratch.write("ca.pem", tls.authority.certificate())};

	kalshi::WebSocketClient ws(kalshi::test::make_signer(), tls.config());
	const kalshi::Result<void> connected = ws.connect();

	ASSERT_FALSE(connected.has_value()) << "accepted a certificate issued to another host";
	EXPECT_EQ(connected.error().code, kalshi::ErrorCode::NetworkError);
}

TEST(WsTls, AnUnknownAuthorityIsRejected) {
	TlsServer tls{kLoopbackNames};
	if (!tls.server.running()) {
		GTEST_SKIP() << "could not start a local TLS WebSocket server";
	}
	const EnvironmentVariable no_file{"SSL_CERT_FILE", std::nullopt};
	const EnvironmentVariable no_directory{"SSL_CERT_DIR", std::nullopt};

	kalshi::WebSocketClient ws(kalshi::test::make_signer(), tls.config());
	const kalshi::Result<void> connected = ws.connect();

	ASSERT_FALSE(connected.has_value());
	EXPECT_EQ(connected.error().code, kalshi::ErrorCode::NetworkError);
}
