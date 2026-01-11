#pragma once

#include "kalshi/error.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace kalshi {

/// Authentication headers returned by the signer
struct AuthHeaders {
	std::string access_key;
	std::string signature;
	std::string timestamp;
};

/// RSA-PSS signer for Kalshi API authentication
///
/// Creates signatures compatible with Kalshi's authentication scheme:
/// - Message format: {timestamp}{method}{path}
/// - Algorithm: RSA-PSS with SHA-256
/// - Salt length: same as digest (32 bytes)
class Signer {
public:
	/// Create a signer from a PEM-encoded RSA private key
	[[nodiscard]] static Result<Signer> from_pem(std::string_view api_key_id,
												 std::string_view pem_key);

	/// Create a signer from a PEM file path
	[[nodiscard]] static Result<Signer> from_pem_file(std::string_view api_key_id,
													  std::string_view file_path);

	~Signer();
	Signer(Signer&&) noexcept;
	Signer& operator=(Signer&&) noexcept;

	// Non-copyable due to OpenSSL key ownership
	Signer(const Signer&) = delete;
	Signer& operator=(const Signer&) = delete;

	/// Generate authentication headers for a request
	[[nodiscard]] Result<AuthHeaders> sign(std::string_view method, std::string_view path) const;

	/// Generate authentication headers with a specific timestamp (for testing)
	[[nodiscard]] Result<AuthHeaders> sign_with_timestamp(std::string_view method,
														  std::string_view path,
														  std::int64_t timestamp_ms) const;

	/// Get the API key ID
	[[nodiscard]] std::string_view api_key_id() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	explicit Signer(std::unique_ptr<Impl> impl);
};

} // namespace kalshi
