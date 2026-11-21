#pragma once

#include "kalshi/error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace kalshi {

/// Headers that authenticate one request.
struct AuthHeaders {
	std::string access_key; ///< KALSHI-ACCESS-KEY
	std::string signature;	///< KALSHI-ACCESS-SIGNATURE (base64)
	std::string timestamp;	///< KALSHI-ACCESS-TIMESTAMP (Unix milliseconds)
};

/// Algorithm of the loaded API key.
enum class KeyType : std::uint8_t {
	Rsa,	///< RSA or RSA-PSS key; signs RSA-PSS with SHA-256 and a digest-length salt
	Ed25519 ///< Ed25519 (RFC 8032)
};

/// Signs Kalshi requests with an API key.
///
/// The signed message is `timestamp + METHOD + path`, where the path starts at
/// `/trade-api/...` and excludes the query string. The algorithm follows the
/// key: RSA keys use RSA-PSS/SHA-256, Ed25519 keys use Ed25519.
///
/// Copies share the same immutable key, and signing is thread-safe.
class Signer {
public:
	/// Loads an unencrypted PEM private key (PKCS#1 or PKCS#8). Encrypted
	/// keys and algorithms other than RSA and Ed25519 are rejected.
	/// RSA-PSS keys load as RSA. Loading signs a test message, so a key that
	/// cannot produce Kalshi's signature (RSA-PSS with SHA-256, MGF1 with
	/// SHA-256, and a 32-byte salt) fails here with ErrorCode::InvalidKey: an
	/// RSA-PSS key restricted to another digest, another MGF1 digest, or a
	/// salt longer than 32 bytes, or an RSA key shorter than 522 bits.
	/// The key ID must be nonempty printable ASCII without spaces.
	[[nodiscard]] static Result<Signer> from_pem(std::string_view api_key_id,
												 std::string_view pem_key);

	/// Reads a PEM private key from a file. See from_pem(). A file that cannot
	/// be opened or read, or that is larger than 1 MiB, fails with
	/// ErrorCode::InvalidKey. On POSIX systems a FIFO is read like any file:
	/// opening it waits for a writer, so this call blocks until one appears.
	/// Running out of memory also fails with ErrorCode::InvalidKey, except that
	/// std::bad_alloc can escape where building that error must allocate: MSVC
	/// builds with a nonzero _ITERATOR_DEBUG_LEVEL (the Debug default), and
	/// libstdc++'s old copy-on-write string ABI.
	[[nodiscard]] static Result<Signer> from_pem_file(std::string_view api_key_id,
													  std::string_view file_path);

	/// Signs a request at the current time.
	[[nodiscard]] Result<AuthHeaders> sign(std::string_view method, std::string_view path) const;

	/// Signs a request at a fixed time. Useful for tests.
	[[nodiscard]] Result<AuthHeaders> sign_with_timestamp(std::string_view method,
														  std::string_view path,
														  std::int64_t timestamp_ms) const;

	[[nodiscard]] std::string_view api_key_id() const noexcept;
	[[nodiscard]] KeyType key_type() const noexcept;

private:
	struct Impl;
	std::shared_ptr<const Impl> impl_;

	explicit Signer(std::shared_ptr<const Impl> impl) noexcept;
};

} // namespace kalshi
