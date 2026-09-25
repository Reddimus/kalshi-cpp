#pragma once

#include "kalshi/signer.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kalshi::test {

/// Encodes a freshly generated key as PEM. `algorithm` is an OpenSSL key type
/// name such as "RSA", "ED25519", or "EC". A non-empty `passphrase` encrypts it.
inline std::string generate_pem(const char* algorithm, std::string_view passphrase = {}) {
	EVP_PKEY* key = std::string_view{algorithm} == "RSA"
						? EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", std::size_t{2048})
					: std::string_view{algorithm} == "EC"
						? EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256")
						: EVP_PKEY_Q_keygen(nullptr, nullptr, algorithm);
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
