#include "kalshi/detail/http_path.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/signer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "support/scratch_directory.hpp"
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

TEST(HttpSigningPath, BaseUrlQueriesAndFragmentsNeverReachTheSignedPath) {
	using kalshi::detail::request_signing_path;
	// A slash inside a query or fragment is not the start of the path.
	EXPECT_EQ(request_signing_path("https://host?x=/y", "/markets"), "/markets");
	EXPECT_EQ(request_signing_path("https://host#f/x", "/markets"), "/markets");
	EXPECT_EQ(request_signing_path("https://host?x=1", "/markets"), "/markets");
	// The path ends at the first `?` or `#`.
	EXPECT_EQ(request_signing_path("https://host/trade-api/v2#frag", "/markets"),
			  "/trade-api/v2/markets");
	EXPECT_EQ(request_signing_path("https://host/trade-api/v2?x=/y#f/z", "/markets"),
			  "/trade-api/v2/markets");
	EXPECT_EQ(request_signing_path("https://host/trade-api/v2/#f?x", "markets?limit=1"),
			  "/trade-api/v2/markets");
	EXPECT_EQ(request_signing_path("https://user@host:8443?next=/a/b", "/markets"), "/markets");
	// `://` only marks a scheme when it comes before the path, query, and fragment.
	EXPECT_EQ(request_signing_path("host/v2?next=https://other/evil", "/markets"), "/v2/markets");
	EXPECT_EQ(request_signing_path("host?next=https://other/evil", "/markets"), "/markets");
	EXPECT_EQ(request_signing_path("host/a://b", "/markets"), "/a://b/markets");
}

TEST(HttpBaseUrl, OnlyHttpBasesThatNameAHostAreAccepted) {
	for (const std::string_view base_url :
		 {"https://api.elections.kalshi.com/trade-api/v2", "http://127.0.0.1:1234/trade-api/v2",
		  "https://[::1]:8080/trade-api/v2", "https://[::1]/trade-api/v2", "HTTPS://Example.com",
		  "http://localhost", "https://user@host/trade-api/v2", "https://user:p@ss@host:443/x",
		  "https://host:/trade-api/v2"}) {
		EXPECT_TRUE(kalshi::detail::names_http_host(base_url)) << base_url;
	}
	for (const std::string_view base_url :
		 {"", "http:", "http://", "http:///", "https:///trade-api/v2", "https://:443/trade-api/v2",
		  "https://@/trade-api/v2", "https://user@/x", "https://user@:443/x", "https://[]:443/x",
		  "https://[::1", "https://[::1]junk/x", "ftp://host/x", "host/trade-api/v2",
		  "https:/host/x"}) {
		EXPECT_FALSE(kalshi::detail::names_http_host(base_url)) << base_url;
	}
}

TEST(HttpSigningPath, EndpointFragmentsAreNotSigned) {
	using kalshi::detail::request_signing_path;
	// libcurl never sends a fragment, so the server cannot have signed over it.
	EXPECT_EQ(request_signing_path("https://host/trade-api/v2", "/markets#frag"),
			  "/trade-api/v2/markets");
	EXPECT_EQ(request_signing_path("https://host/trade-api/v2", "/markets#frag?limit=1"),
			  "/trade-api/v2/markets");
	EXPECT_EQ(request_signing_path("https://host/trade-api/v2", "/markets?limit=1#frag"),
			  "/trade-api/v2/markets");
}

TEST(HttpSigningPath, BasesWithoutAPathOrSchemeKeepTheirDocumentedJoin) {
	using kalshi::detail::request_signing_path;
	EXPECT_EQ(request_signing_path("https://host", "/markets"), "/markets");
	EXPECT_EQ(request_signing_path("https://host", "markets"), "/markets");
	EXPECT_EQ(request_signing_path("https://host/", "/markets"), "/markets");
	EXPECT_EQ(request_signing_path("https://host:8443/v2", "markets"), "/v2/markets");
	EXPECT_EQ(request_signing_path("host/v2", "/markets"), "/v2/markets");
	EXPECT_EQ(request_signing_path("https://host/v2", ""), "/v2");
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

/// Key files larger than this are rejected. Mirrors the limit in signer.cpp.
constexpr std::size_t kMaxKeyFileBytes = std::size_t{1024} * 1024;

/// `pem` followed by enough blank lines to make it `size` bytes long.
std::string padded_to(const std::string& pem, std::size_t size) {
	std::string padded = pem;
	padded.resize(size, '\n');
	return padded;
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

	EXPECT_TRUE(
		kalshi::test::signature_verifies(pem, "1234567890000GET" + path, headers->signature));
	// A different path, method, or timestamp must not verify — otherwise
	// the assertion above would pass for any signed message.
	EXPECT_FALSE(kalshi::test::signature_verifies(pem, "1234567890000GET" + path + "?token=value",
												  headers->signature));
	EXPECT_FALSE(
		kalshi::test::signature_verifies(pem, "1234567890000POST" + path, headers->signature));
	EXPECT_FALSE(
		kalshi::test::signature_verifies(pem, "1234567890001GET" + path, headers->signature));
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
	EXPECT_TRUE(
		kalshi::test::signature_verifies(pem, "1234567890000GET" + path, headers->signature));
	EXPECT_FALSE(
		kalshi::test::signature_verifies(pem, "1234567890000GET/other", headers->signature));
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
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
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

TEST(Signer, RejectsEmptyIdsAndHeaderControlBytes) {
	for (const std::string& id : {std::string{}, std::string("key\r\nInjected: value"),
								  std::string("key\0suffix", 10), std::string("key id")}) {
		const kalshi::Result<kalshi::Signer> result =
			kalshi::Signer::from_pem(id, kalshi::test::private_key_pem());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidKey);
	}
}

TEST(Signer, RsaPssKeysLoadAsRsaAndSign) {
	const std::string pem = kalshi::test::generate_rsa_pss_pem();
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("pss-key", pem);
	ASSERT_TRUE(signer.has_value()) << signer.error().message;
	EXPECT_EQ(signer->key_type(), kalshi::KeyType::Rsa);

	const std::string path = "/trade-api/v2/portfolio/balance";
	const kalshi::Result<kalshi::AuthHeaders> headers =
		signer->sign_with_timestamp("GET", path, 1234567890000);
	ASSERT_TRUE(headers.has_value()) << headers.error().message;
	EXPECT_TRUE(
		kalshi::test::signature_verifies(pem, "1234567890000GET" + path, headers->signature));
	EXPECT_FALSE(
		kalshi::test::signature_verifies(pem, "1234567890000GET/other", headers->signature));
}

TEST(Signer, RsaPssKeysRestrictedToKalshisParametersLoadAndSign) {
	// SHA-256 for the digest and MGF1 with a salt of at least 32 bytes, or a
	// shorter minimum, is what the signer produces anyway.
	for (const int salt_length : {32, 20}) {
		const std::string pem =
			kalshi::test::generate_rsa_pss_pem(EVP_sha256(), EVP_sha256(), salt_length);
		const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("pss-key", pem);
		ASSERT_TRUE(signer.has_value()) << signer.error().message;
		EXPECT_EQ(signer->key_type(), kalshi::KeyType::Rsa);
		const kalshi::Result<kalshi::AuthHeaders> headers =
			signer->sign_with_timestamp("GET", "/trade-api/v2/markets", 1234567890000);
		ASSERT_TRUE(headers.has_value()) << headers.error().message;
		EXPECT_TRUE(kalshi::test::signature_verifies(pem, "1234567890000GET/trade-api/v2/markets",
													 headers->signature));
	}
}

TEST(Signer, RsaPssKeysThatCannotSignForKalshiFailAtLoadTime) {
	struct Restriction {
		const char* name;
		const EVP_MD* digest;
		const EVP_MD* mgf1;
		int salt_length;
	};
	for (const Restriction& restriction :
		 {Restriction{"digest pinned to SHA-384", EVP_sha384(), EVP_sha384(), 48},
		  Restriction{"minimum salt above the digest length", EVP_sha256(), EVP_sha256(), 64},
		  Restriction{"MGF1 pinned to SHA-384", EVP_sha256(), EVP_sha384(), 32}}) {
		const std::string pem = kalshi::test::generate_rsa_pss_pem(
			restriction.digest, restriction.mgf1, restriction.salt_length);
		const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("pss-key", pem);
		ASSERT_FALSE(signer.has_value()) << restriction.name;
		EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey) << restriction.name;
		EXPECT_NE(signer.error().message.find("cannot produce the signature"), std::string::npos)
			<< signer.error().message;
	}
}

TEST(Signer, RsaKeysTooShortForKalshisSignatureFailAtLoadTime) {
	// RSA-PSS with SHA-256 and a 32-byte salt needs a modulus of at least 522 bits.
	std::string pem;
	try {
		pem = kalshi::test::generate_pem("RSA", {}, 512);
	} catch (const std::runtime_error&) {
		GTEST_SKIP() << "this OpenSSL build does not generate 512-bit RSA keys";
	}
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem("short-key", pem);
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
}

TEST(SignerKeyFile, LoadsAKeyFromAFile) {
	const kalshi::test::ScratchDirectory scratch;
	const std::string path = scratch.write("kalshi.pem", kalshi::test::private_key_pem());
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("key", path);
	ASSERT_TRUE(signer.has_value()) << signer.error().message;
	EXPECT_EQ(signer->key_type(), kalshi::KeyType::Rsa);
	EXPECT_TRUE(signer->sign("GET", "/trade-api/v2/markets").has_value());
}

TEST(SignerKeyFile, DirectoriesFailAsErrorsNotExceptions) {
	const kalshi::test::ScratchDirectory scratch;
	kalshi::Result<kalshi::Signer> signer =
		std::unexpected(kalshi::Error{kalshi::ErrorCode::Unknown, "not called"});
	ASSERT_NO_THROW(signer = kalshi::Signer::from_pem_file("key", scratch.path()));
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
	// "Failed to read key file" where a directory opens, "Failed to open" elsewhere.
	EXPECT_NE(signer.error().message.find("key file '" + scratch.path() + "'"), std::string::npos)
		<< signer.error().message;
}

TEST(SignerKeyFile, EmptyFilesAreInvalidKeys) {
	const kalshi::test::ScratchDirectory scratch;
	const kalshi::Result<kalshi::Signer> signer =
		kalshi::Signer::from_pem_file("key", scratch.write("empty.pem", ""));
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
}

TEST(SignerKeyFile, FilesOverTheSizeLimitAreRejectedWhateverTheyHold) {
	const kalshi::test::ScratchDirectory scratch;
	// Without the limit this file loads: the PEM parser stops at the key's end.
	const std::string path = scratch.write(
		"padded.pem", padded_to(kalshi::test::private_key_pem(), kMaxKeyFileBytes + 1));
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("key", path);
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
	EXPECT_NE(signer.error().message.find("Key file '" + path + "' is larger than"),
			  std::string::npos)
		<< signer.error().message;
}

TEST(SignerKeyFile, FilesAtTheSizeLimitStillLoad) {
	const kalshi::test::ScratchDirectory scratch;
	const std::string path =
		scratch.write("padded.pem", padded_to(kalshi::test::private_key_pem(), kMaxKeyFileBytes));
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("key", path);
	ASSERT_TRUE(signer.has_value()) << signer.error().message;
}

#ifndef _WIN32
TEST(SignerKeyFile, EndlessDevicesAreRejectedInsteadOfReadForever) {
	if (!std::ifstream{"/dev/zero", std::ios::binary}) {
		GTEST_SKIP() << "/dev/zero is not readable here";
	}
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("key", "/dev/zero");
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
	EXPECT_NE(signer.error().message.find("larger than"), std::string::npos)
		<< signer.error().message;
}

namespace {

std::atomic<int> interruptions{0};

void count_interruption(int /*signal*/) {
	interruptions.fetch_add(1, std::memory_order_relaxed);
}

/// Opens the FIFO's write end once a reader has it open, or returns -1 after
/// `deadline`, so a reader that gave up fails the test instead of hanging it.
int open_fifo_writer(const std::string& path, std::chrono::steady_clock::time_point deadline) {
	while (std::chrono::steady_clock::now() < deadline) {
		const int fd = ::open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0) {
			const int flags = ::fcntl(fd, F_GETFL);
			if (flags >= 0 && ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0) {
				return fd;
			}
			(void)::close(fd);
			return -1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{5});
	}
	return -1;
}

bool write_all(int fd, std::string_view bytes) {
	while (!bytes.empty()) {
		const ssize_t written = ::write(fd, bytes.data(), bytes.size());
		if (written < 0 && errno == EINTR) {
			continue;
		}
		if (written <= 0) {
			return false;
		}
		bytes.remove_prefix(static_cast<std::size_t>(written));
	}
	return true;
}

/// Loads a key through a FIFO, the way a secrets manager or a shell's process
/// substitution hands one over, while SIGUSR1, handled without SA_RESTART,
/// interrupts the reader `open_signals` times as it waits in open() for a
/// writer and `read_signals` times as it waits in read() for the rest of the
/// key. Each interrupted call fails with EINTR, and the load must retry it.
void expect_key_loads_through_fifo(int open_signals, int read_signals) {
	const kalshi::test::ScratchDirectory scratch;
	const std::string path = scratch.path() + "/kalshi.pem";
	ASSERT_EQ(::mkfifo(path.c_str(), 0600), 0) << "mkfifo failed with errno " << errno;

	struct sigaction interrupt {};
	interrupt.sa_handler = count_interruption;
	sigemptyset(&interrupt.sa_mask);
	interrupt.sa_flags = 0; // no SA_RESTART, so blocked calls return EINTR
	struct sigaction previous_usr1 {};
	ASSERT_EQ(::sigaction(SIGUSR1, &interrupt, &previous_usr1), 0);
	// A reader that gives up early would turn the writer's next write into
	// SIGPIPE, which on macOS goes to the whole process.
	struct sigaction ignore {};
	ignore.sa_handler = SIG_IGN;
	sigemptyset(&ignore.sa_mask);
	struct sigaction previous_pipe {};
	ASSERT_EQ(::sigaction(SIGPIPE, &ignore, &previous_pipe), 0);
	interruptions = 0;

	const std::string& pem = kalshi::test::private_key_pem();
	const pthread_t reader = ::pthread_self();
	bool delivered = false;
	std::thread writer{[&] {
		const std::chrono::milliseconds pause{30};
		// No writer yet, so the reader is blocked in open().
		for (int i = 0; i < open_signals; ++i) {
			std::this_thread::sleep_for(pause);
			(void)::pthread_kill(reader, SIGUSR1);
		}
		const int fd =
			open_fifo_writer(path, std::chrono::steady_clock::now() + std::chrono::seconds{10});
		if (fd < 0) {
			return;
		}
		const std::string_view text{pem};
		const std::size_t half = text.size() / 2;
		bool written = write_all(fd, text.substr(0, half));
		// The reader has the first half and is blocked in read() for the rest.
		for (int i = 0; i < read_signals && written; ++i) {
			std::this_thread::sleep_for(pause);
			(void)::pthread_kill(reader, SIGUSR1);
		}
		written = written && write_all(fd, text.substr(half));
		delivered = ::close(fd) == 0 && written;
	}};
	const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("key", path);
	writer.join();
	(void)::sigaction(SIGPIPE, &previous_pipe, nullptr);
	(void)::sigaction(SIGUSR1, &previous_usr1, nullptr);

	ASSERT_TRUE(signer.has_value()) << signer.error().message;
	EXPECT_TRUE(delivered);
	EXPECT_EQ(interruptions.load(), open_signals + read_signals);
	EXPECT_TRUE(signer->sign("GET", "/trade-api/v2/markets").has_value());
}

} // namespace

TEST(SignerKeyFile, SignalsInterruptingAFifoReadDoNotFailTheLoad) {
	expect_key_loads_through_fifo(0, 2);
}

TEST(SignerKeyFile, SignalsInterruptingAFifoOpenDoNotFailTheLoad) {
	expect_key_loads_through_fifo(2, 0);
}
#endif

#ifdef __linux__
TEST(SignerKeyFile, FilesWhoseReadsFailAreErrorsNotExceptions) {
	// Opens, but the first read fails with EIO.
	kalshi::Result<kalshi::Signer> signer =
		std::unexpected(kalshi::Error{kalshi::ErrorCode::Unknown, "not called"});
	ASSERT_NO_THROW(signer = kalshi::Signer::from_pem_file("key", "/proc/self/mem"));
	ASSERT_FALSE(signer.has_value());
	EXPECT_EQ(signer.error().code, kalshi::ErrorCode::InvalidKey);
	EXPECT_NE(signer.error().message.find("Failed to read key file '/proc/self/mem'"),
			  std::string::npos)
		<< signer.error().message;
}
#endif
