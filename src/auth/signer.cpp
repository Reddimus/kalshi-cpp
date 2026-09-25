#include "kalshi/signer.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <string>
#include <vector>

namespace kalshi {

namespace {

struct PkeyDeleter {
	void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct MdCtxDeleter {
	void operator()(EVP_MD_CTX* ctx) const noexcept { EVP_MD_CTX_free(ctx); }
};
struct BioDeleter {
	void operator()(BIO* bio) const noexcept { BIO_free(bio); }
};

using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, MdCtxDeleter>;
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

/// Drains OpenSSL's thread-local error queue into one message.
std::string openssl_error(std::string_view what) {
	std::string message{what};
	unsigned long code = 0;
	std::array<char, 256> buffer{};
	while ((code = ERR_get_error()) != 0) {
		ERR_error_string_n(code, buffer.data(), buffer.size());
		message += ": ";
		message += buffer.data();
	}
	return message;
}

Error signing_error(std::string_view what) {
	return Error::signing(openssl_error(what));
}

/// Refuses encrypted keys instead of letting OpenSSL prompt on the terminal.
int no_passphrase(char* /*buffer*/, int /*size*/, int /*rwflag*/, void* /*userdata*/) {
	return -1;
}

std::string base64(const std::vector<unsigned char>& bytes) {
	std::string encoded(4 * ((bytes.size() + 2) / 3), '\0');
	const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
										bytes.data(), static_cast<int>(bytes.size()));
	encoded.resize(static_cast<std::size_t>(written));
	return encoded;
}

} // namespace

struct Signer::Impl {
	std::string api_key_id;
	PkeyPtr key;
	KeyType type{KeyType::Rsa};
};

Signer::Signer(std::shared_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}

std::string_view Signer::api_key_id() const noexcept {
	return impl_ ? std::string_view{impl_->api_key_id} : std::string_view{};
}

KeyType Signer::key_type() const noexcept {
	return impl_ ? impl_->type : KeyType::Rsa;
}

Result<Signer> Signer::from_pem(std::string_view api_key_id, std::string_view pem_key) {
	ERR_clear_error();
	const BioPtr bio{BIO_new_mem_buf(pem_key.data(), static_cast<int>(pem_key.size()))};
	if (!bio) {
		return std::unexpected(signing_error("Failed to allocate a key buffer"));
	}
	PkeyPtr key{PEM_read_bio_PrivateKey(bio.get(), nullptr, no_passphrase, nullptr)};
	if (!key) {
		return std::unexpected(signing_error("Failed to read the private key (it must be an "
											 "unencrypted PEM key)"));
	}

	KeyType type = KeyType::Rsa;
	switch (EVP_PKEY_get_base_id(key.get())) {
		case EVP_PKEY_RSA:
			type = KeyType::Rsa;
			break;
		case EVP_PKEY_ED25519:
			type = KeyType::Ed25519;
			break;
		default:
			return std::unexpected(
				Error{ErrorCode::InvalidKey,
					  "Unsupported key type: Kalshi accepts RSA and Ed25519 keys"});
	}

	std::shared_ptr<Impl> impl = std::make_shared<Impl>();
	impl->api_key_id = std::string(api_key_id);
	impl->key = std::move(key);
	impl->type = type;
	return Signer(std::move(impl));
}

Result<Signer> Signer::from_pem_file(std::string_view api_key_id, std::string_view file_path) {
	std::ifstream file{std::string(file_path), std::ios::binary};
	if (!file) {
		return std::unexpected(
			Error::signing("Failed to open key file '" + std::string(file_path) + "'"));
	}
	std::string pem{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	Result<Signer> signer = from_pem(api_key_id, pem);
	OPENSSL_cleanse(pem.data(), pem.size());
	return signer;
}

Result<AuthHeaders> Signer::sign(std::string_view method, std::string_view path) const {
	const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
									std::chrono::system_clock::now().time_since_epoch())
									.count();
	return sign_with_timestamp(method, path, now_ms);
}

Result<AuthHeaders> Signer::sign_with_timestamp(std::string_view method, std::string_view path,
												std::int64_t timestamp_ms) const {
	if (!impl_) {
		return std::unexpected(Error::signing("Signer is empty (moved-from)"));
	}
	std::string timestamp = std::to_string(timestamp_ms);
	std::string message;
	message.reserve(timestamp.size() + method.size() + path.size());
	message.append(timestamp).append(method).append(path);

	ERR_clear_error();
	const MdCtxPtr ctx{EVP_MD_CTX_new()};
	if (!ctx) {
		return std::unexpected(signing_error("Failed to create a signing context"));
	}
	EVP_PKEY_CTX* pkey_ctx = nullptr;
	// Ed25519 signs the message directly, so it takes no digest.
	const EVP_MD* digest = impl_->type == KeyType::Rsa ? EVP_sha256() : nullptr;
	if (EVP_DigestSignInit(ctx.get(), &pkey_ctx, digest, nullptr, impl_->key.get()) != 1) {
		return std::unexpected(signing_error("Failed to initialize signing"));
	}
	if (impl_->type == KeyType::Rsa &&
		(EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1 ||
		 EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) != 1)) {
		return std::unexpected(signing_error("Failed to configure RSA-PSS"));
	}

	const unsigned char* data = reinterpret_cast<const unsigned char*>(message.data());
	std::size_t signature_size = 0;
	if (EVP_DigestSign(ctx.get(), nullptr, &signature_size, data, message.size()) != 1) {
		return std::unexpected(signing_error("Failed to size the signature"));
	}
	std::vector<unsigned char> signature(signature_size);
	if (EVP_DigestSign(ctx.get(), signature.data(), &signature_size, data, message.size()) != 1) {
		return std::unexpected(signing_error("Failed to sign"));
	}
	signature.resize(signature_size);

	return AuthHeaders{.access_key = impl_->api_key_id,
					   .signature = base64(signature),
					   .timestamp = std::move(timestamp)};
}

} // namespace kalshi
