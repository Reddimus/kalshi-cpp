#pragma once

#include "kalshi/signer.hpp"

#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kalshi::test {

inline const std::string& private_key_pem() {
	static const std::string pem = [] {
		EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
		if (!context)
			throw std::runtime_error("Failed to create the test RSA context");

		EVP_PKEY* key = nullptr;
		const bool generated = EVP_PKEY_keygen_init(context) == 1 &&
							   EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) == 1 &&
							   EVP_PKEY_keygen(context, &key) == 1;
		EVP_PKEY_CTX_free(context);
		if (!generated) {
			EVP_PKEY_free(key);
			throw std::runtime_error("Failed to generate the test RSA key");
		}

		BIO* output = BIO_new(BIO_s_mem());
		if (!output ||
			PEM_write_bio_PrivateKey(output, key, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
			BIO_free(output);
			EVP_PKEY_free(key);
			throw std::runtime_error("Failed to encode the test RSA key");
		}
		BUF_MEM* buffer = nullptr;
		BIO_get_mem_ptr(output, &buffer);
		std::string result(buffer->data, buffer->length);
		BIO_free(output);
		EVP_PKEY_free(key);
		return result;
	}();
	return pem;
}

inline Signer make_signer(std::string_view key_id = "test-api-key-id") {
	Result<Signer> result = Signer::from_pem(key_id, private_key_pem());
	if (!result)
		throw std::runtime_error("Failed to parse the generated test RSA key");
	return std::move(*result);
}

} // namespace kalshi::test
