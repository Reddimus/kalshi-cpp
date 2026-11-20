#pragma once

#include "kalshi/signer.hpp"

#include <cstddef>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

/// The public half of an RSA or RSA-PSS key as a plain RSA key. A verifier that
/// holds only the modulus and exponent applies none of an RSA-PSS key's
/// parameter restrictions, so it accepts exactly the signature it asks for.
inline EVP_PKEY* plain_rsa_public_key(const EVP_PKEY* key) {
	BIGNUM* modulus = nullptr;
	BIGNUM* exponent = nullptr;
	OSSL_PARAM_BLD* builder = OSSL_PARAM_BLD_new();
	OSSL_PARAM* params = nullptr;
	EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
	EVP_PKEY* plain = nullptr;
	if (builder != nullptr && ctx != nullptr &&
		EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_N, &modulus) == 1 &&
		EVP_PKEY_get_bn_param(key, OSSL_PKEY_PARAM_RSA_E, &exponent) == 1 &&
		OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_RSA_N, modulus) == 1 &&
		OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_RSA_E, exponent) == 1 &&
		(params = OSSL_PARAM_BLD_to_param(builder)) != nullptr &&
		EVP_PKEY_fromdata_init(ctx) == 1) {
		(void)EVP_PKEY_fromdata(ctx, &plain, EVP_PKEY_PUBLIC_KEY, params);
	}
	EVP_PKEY_CTX_free(ctx);
	OSSL_PARAM_free(params);
	OSSL_PARAM_BLD_free(builder);
	BN_free(exponent);
	BN_free(modulus);
	return plain;
}

/// Verify a signature the way Kalshi's gateway does: RSA-PSS/SHA-256 or Ed25519.
///
/// RSA-PSS salts every signature, so two calls over the same input are
/// never byte-identical and cannot be pinned with a golden string. What
/// *is* stable, and what a signing regression would break, is that the
/// emitted signature verifies against the key over exactly
/// `timestamp + method + path`.
inline bool signature_verifies(const std::string& pem, const std::string& message,
							   const std::string& base64_signature) {
	BIO* key_bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
	if (!key_bio)
		return false;
	EVP_PKEY* key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
	BIO_free(key_bio);
	if (!key)
		return false;
	const int base_id = EVP_PKEY_get_base_id(key);
	const bool rsa = base_id == EVP_PKEY_RSA || base_id == EVP_PKEY_RSA_PSS;
	if (rsa) {
		EVP_PKEY* plain = plain_rsa_public_key(key);
		EVP_PKEY_free(key);
		key = plain;
		if (!key)
			return false;
	}

	std::vector<unsigned char> signature(base64_signature.size());
	const int decoded_with_padding = EVP_DecodeBlock(
		signature.data(), reinterpret_cast<const unsigned char*>(base64_signature.data()),
		static_cast<int>(base64_signature.size()));
	if (decoded_with_padding < 0) {
		EVP_PKEY_free(key);
		return false;
	}
	std::size_t padding = 0;
	while (padding < base64_signature.size() &&
		   base64_signature[base64_signature.size() - 1 - padding] == '=')
		++padding;
	signature.resize(static_cast<std::size_t>(decoded_with_padding) - padding);

	EVP_MD_CTX* ctx = EVP_MD_CTX_new();
	EVP_PKEY_CTX* pkey_ctx = nullptr;
	const bool verified =
		ctx &&
		EVP_DigestVerifyInit(ctx, &pkey_ctx, rsa ? EVP_sha256() : nullptr, nullptr, key) == 1 &&
		(!rsa || (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) == 1 &&
				  EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, EVP_sha256()) == 1 &&
				  EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) == 1)) &&
		EVP_DigestVerify(ctx, signature.data(), signature.size(),
						 reinterpret_cast<const unsigned char*>(message.data()),
						 message.size()) == 1;
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(key);
	return verified;
}

inline Signer make_signer(std::string_view key_id = "test-api-key-id") {
	Result<Signer> result = Signer::from_pem(key_id, private_key_pem());
	if (!result)
		throw std::runtime_error("Failed to parse the generated test RSA key");
	return std::move(*result);
}

} // namespace kalshi::test
