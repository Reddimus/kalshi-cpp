#include "kalshi/signer.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <string>
#include <string_view>
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

struct FileCloser {
	void operator()(std::FILE* file) const noexcept { (void)std::fclose(file); }
};

using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, MdCtxDeleter>;
using BioPtr = std::unique_ptr<BIO, BioDeleter>;
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

/// PEM keys are a few kilobytes. The limit keeps a device or a mistaken path
/// from being read without end.
constexpr std::size_t kMaxKeyFileBytes = std::size_t{1024} * 1024;

/// Holds a key file's bytes in one allocation, sized up front so that reading
/// never moves them, and wipes it before the memory is freed. One byte more
/// than the limit tells a file at the limit from a longer one.
class KeyFileBuffer {
public:
	static constexpr std::size_t kCapacity = kMaxKeyFileBytes + 1;

	KeyFileBuffer() : bytes_(std::make_unique_for_overwrite<char[]>(kCapacity)) {}
	~KeyFileBuffer() { OPENSSL_cleanse(bytes_.get(), kCapacity); }
	KeyFileBuffer(const KeyFileBuffer&) = delete;
	KeyFileBuffer& operator=(const KeyFileBuffer&) = delete;

	[[nodiscard]] char* data() noexcept { return bytes_.get(); }

private:
	std::unique_ptr<char[]> bytes_;
};

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

/// Signs `message` the way Kalshi verifies it: RSA-PSS with SHA-256 as the
/// digest and the MGF1 digest and a salt as long as the digest, or Ed25519.
/// from_pem() signs once with this to check a key, so every setting a key could
/// refuse belongs here.
Result<std::vector<unsigned char>> sign_message(EVP_PKEY* key, KeyType type,
												std::string_view message) {
	ERR_clear_error();
	const MdCtxPtr ctx{EVP_MD_CTX_new()};
	if (!ctx) {
		return std::unexpected(signing_error("Failed to create a signing context"));
	}
	EVP_PKEY_CTX* pkey_ctx = nullptr;
	// Ed25519 signs the message directly, so it takes no digest.
	const EVP_MD* digest = type == KeyType::Rsa ? EVP_sha256() : nullptr;
	if (EVP_DigestSignInit(ctx.get(), &pkey_ctx, digest, nullptr, key) != 1) {
		return std::unexpected(signing_error("Failed to initialize signing"));
	}
	// MGF1 uses the signing digest unless an RSA-PSS key's parameters name
	// another one. Asking for SHA-256 makes such a key fail instead of producing
	// a signature Kalshi cannot verify.
	if (type == KeyType::Rsa &&
		(EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1 ||
		 EVP_PKEY_CTX_set_rsa_mgf1_md(pkey_ctx, EVP_sha256()) != 1 ||
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
	return signature;
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
	if (api_key_id.empty()) {
		return std::unexpected(Error{ErrorCode::InvalidKey, "API key ID must not be empty"});
	}
	for (const unsigned char byte : api_key_id) {
		if (byte <= 0x20 || byte >= 0x7f) {
			return std::unexpected(
				Error{ErrorCode::InvalidKey,
					  "API key ID must contain only printable ASCII without spaces"});
		}
	}
	if (pem_key.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		return std::unexpected(
			Error{ErrorCode::InvalidKey, "PEM key exceeds OpenSSL's buffer limit"});
	}
	ERR_clear_error();
	const BioPtr bio{BIO_new_mem_buf(pem_key.data(), static_cast<int>(pem_key.size()))};
	if (!bio) {
		return std::unexpected(signing_error("Failed to allocate a key buffer"));
	}
	PkeyPtr key{PEM_read_bio_PrivateKey(bio.get(), nullptr, no_passphrase, nullptr)};
	if (!key) {
		return std::unexpected(Error{ErrorCode::InvalidKey,
									 openssl_error("Failed to read the private key (it must be an "
												   "unencrypted PEM key)")});
	}

	KeyType type = KeyType::Rsa;
	switch (EVP_PKEY_get_base_id(key.get())) {
		case EVP_PKEY_RSA:
		// An RSA-PSS key is an RSA key that may only make PSS signatures.
		case EVP_PKEY_RSA_PSS:
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

	// Some keys load but can never sign for Kalshi: an RSA-PSS key restricted to
	// another digest, another MGF1 digest, or a longer salt, or an RSA key too
	// short for the padding.
	// Signing once finds them here instead of on every request.
	const Result<std::vector<unsigned char>> trial =
		sign_message(key.get(), type, "kalshi-cpp key check");
	if (!trial) {
		return std::unexpected(Error{
			ErrorCode::InvalidKey,
			"Key cannot produce the signature Kalshi requires (" +
				std::string(type == KeyType::Rsa
								? "RSA-PSS with SHA-256, MGF1 with SHA-256, and a 32-byte salt"
								: "Ed25519") +
				"): " + trial.error().message});
	}

	std::shared_ptr<Impl> impl = std::make_shared<Impl>();
	impl->api_key_id = std::string(api_key_id);
	impl->key = std::move(key);
	impl->type = type;
	return Signer(std::move(impl));
}

Result<Signer> Signer::from_pem_file(std::string_view api_key_id, std::string_view file_path) {
	// Nothing may escape: the path string, the buffer, and from_pem() allocate.
	try {
		const std::string path{file_path};
		const FilePtr file{std::fopen(path.c_str(), "rb")};
		if (!file) {
			return std::unexpected(
				Error{ErrorCode::InvalidKey, "Failed to open key file '" + path + "'"});
		}
		// Unbuffered, so the stream keeps no copy of the key: each read goes
		// straight into `buffer`, which is wiped when this function returns.
		if (std::setvbuf(file.get(), nullptr, _IONBF, 0) != 0) {
			return std::unexpected(
				Error{ErrorCode::InvalidKey, "Failed to read key file '" + path + "'"});
		}
		KeyFileBuffer buffer;
		std::size_t size = 0;
		while (size < KeyFileBuffer::kCapacity) {
			errno = 0;
			const std::size_t count =
				std::fread(buffer.data() + size, 1, KeyFileBuffer::kCapacity - size, file.get());
			size += count;
			if (std::ferror(file.get()) != 0 && errno == EINTR) {
				// A signal interrupted a read from a pipe; nothing was lost.
				std::clearerr(file.get());
				continue;
			}
			if (count == 0) {
				break; // end of file, or an error that ferror() reports below
			}
		}
		// A directory or a device can open and then fail to read.
		if (std::ferror(file.get()) != 0) {
			return std::unexpected(
				Error{ErrorCode::InvalidKey, "Failed to read key file '" + path + "'"});
		}
		if (size > kMaxKeyFileBytes) {
			return std::unexpected(
				Error{ErrorCode::InvalidKey, "Key file '" + path + "' is larger than 1 MiB"});
		}
		return from_pem(api_key_id, std::string_view{buffer.data(), size});
	} catch (...) {
		try {
			return std::unexpected(Error{ErrorCode::InvalidKey, "Failed to read key file '" +
																	std::string(file_path) + "'"});
		} catch (...) {
			// Out of memory even for the message. This one fits the small-string
			// storage of libstdc++, libc++, and MSVC's release library, so
			// building it does not allocate.
			return std::unexpected(Error{ErrorCode::InvalidKey, "Key read failed"});
		}
	}
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

	Result<std::vector<unsigned char>> signature =
		sign_message(impl_->key.get(), impl_->type, message);
	if (!signature) {
		return std::unexpected(std::move(signature.error()));
	}
	return AuthHeaders{.access_key = impl_->api_key_id,
					   .signature = base64(*signature),
					   .timestamp = std::move(timestamp)};
}

} // namespace kalshi
