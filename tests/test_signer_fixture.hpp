#pragma once

#include "kalshi/signer.hpp"

#include <cstddef>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kalshi::test {

/// Encodes `key` as PEM and frees it. A non-empty `passphrase` encrypts it.
inline std::string encode_pem(EVP_PKEY* key, std::string_view passphrase = {}) {
	if (key == nullptr) {
		throw std::runtime_error("Failed to generate a test key");
	}
	BIO* output = BIO_new(BIO_s_mem());
	const EVP_CIPHER* cipher = passphrase.empty() ? nullptr : EVP_aes_256_cbc();
	const bool written =
		output != nullptr &&
		PEM_write_bio_PrivateKey(output, key, cipher,
								 reinterpret_cast<const unsigned char*>(passphrase.data()),
								 static_cast<int>(passphrase.size()), nullptr, nullptr) == 1;
	EVP_PKEY_free(key);
	if (!written) {
		BIO_free(output);
		throw std::runtime_error("Failed to encode a test key");
	}
	char* data = nullptr;
	const long size = BIO_get_mem_data(output, &data);
	std::string pem(data, static_cast<std::size_t>(size));
	BIO_free(output);
	return pem;
}

/// Encodes a new key as PEM. `algorithm` is an OpenSSL key type name such as
/// "RSA", "ED25519", or "EC". `rsa_bits` sizes an "RSA" key. A non-empty
/// `passphrase` encrypts the PEM.
inline std::string generate_pem(const char* algorithm, std::string_view passphrase = {},
								std::size_t rsa_bits = 2048) {
	return encode_pem(
		std::string_view{algorithm} == "RSA"  ? EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", rsa_bits)
		: std::string_view{algorithm} == "EC" ? EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256")
											  : EVP_PKEY_Q_keygen(nullptr, nullptr, algorithm),
		passphrase);
}

/// Makes an RSA-PSS key. With a `digest`, the key's parameters restrict it
/// (RFC 4055) to signing with that digest, the MGF1 digest `mgf1`, and a salt of
/// at least `salt_length` bytes; without one the key has no restrictions.
/// OpenSSL 3.0's EVP_PKEY_Q_keygen() does not make RSA-PSS keys, so this goes
/// through a key context.
inline std::string generate_rsa_pss_pem(const EVP_MD* digest = nullptr,
										const EVP_MD* mgf1 = nullptr, int salt_length = 0) {
	EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA-PSS", nullptr);
	EVP_PKEY* key = nullptr;
	const bool created =
		ctx != nullptr && EVP_PKEY_keygen_init(ctx) == 1 &&
		EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) == 1 &&
		(digest == nullptr || (EVP_PKEY_CTX_set_rsa_pss_keygen_md(ctx, digest) == 1 &&
							   EVP_PKEY_CTX_set_rsa_pss_keygen_mgf1_md(ctx, mgf1) == 1 &&
							   EVP_PKEY_CTX_set_rsa_pss_keygen_saltlen(ctx, salt_length) == 1)) &&
		EVP_PKEY_keygen(ctx, &key) == 1;
	EVP_PKEY_CTX_free(ctx);
	return encode_pem(created ? key : nullptr);
}

inline const std::string& private_key_pem() {
	static const std::string pem = generate_pem("RSA");
	return pem;
}

inline const std::string& ed25519_private_key_pem() {
	static const std::string pem = generate_pem("ED25519");
	return pem;
}

inline Signer make_signer(std::string_view key_id = "test-api-key-id") {
	Result<Signer> result = Signer::from_pem(key_id, private_key_pem());
	if (!result)
		throw std::runtime_error("Failed to parse the generated test RSA key");
	return std::move(*result);
}

} // namespace kalshi::test
