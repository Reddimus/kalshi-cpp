#include "kalshi/signer.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <sstream>
#include <vector>

namespace kalshi {

struct Signer::Impl {
    std::string api_key_id;
    EVP_PKEY* pkey{nullptr};

    ~Impl() {
        if (pkey) {
            EVP_PKEY_free(pkey);
        }
    }
};

Signer::Signer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Signer::~Signer() = default;

Signer::Signer(Signer&&) noexcept = default;

Signer& Signer::operator=(Signer&&) noexcept = default;

std::string_view Signer::api_key_id() const noexcept {
    return impl_->api_key_id;
}

namespace {

std::string get_openssl_error() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
}

std::string base64_encode(const unsigned char* data, size_t len) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, bio);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

} // namespace

Result<Signer> Signer::from_pem(std::string_view api_key_id, std::string_view pem_key) {
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->api_key_id = std::string(api_key_id);

    BIO* bio = BIO_new_mem_buf(pem_key.data(), static_cast<int>(pem_key.size()));
    if (!bio) {
        return std::unexpected(Error::signing("Failed to create BIO"));
    }

    impl->pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!impl->pkey) {
        return std::unexpected(
            Error::signing("Failed to read private key: " + get_openssl_error()));
    }

    return Signer(std::move(impl));
}

Result<Signer> Signer::from_pem_file(std::string_view api_key_id, std::string_view file_path) {
    std::ifstream file{std::string(file_path)};
    if (!file) {
        return std::unexpected(Error::signing("Failed to open key file"));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return from_pem(api_key_id, buffer.str());
}

Result<AuthHeaders> Signer::sign(std::string_view method, std::string_view path) const {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return sign_with_timestamp(method, path, ms);
}

Result<AuthHeaders> Signer::sign_with_timestamp(std::string_view method, std::string_view path,
                                                std::int64_t timestamp_ms) const {
    // Build message: timestamp + method + path
    std::string timestamp_str = std::to_string(timestamp_ms);
    std::string message = timestamp_str + std::string(method) + std::string(path);

    // Create signing context
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return std::unexpected(Error::signing("Failed to create signing context"));
    }

    // Initialize for RSA-PSS with SHA-256
    EVP_PKEY_CTX* pkey_ctx = nullptr;
    if (EVP_DigestSignInit(ctx, &pkey_ctx, EVP_sha256(), nullptr, impl_->pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(Error::signing("Failed to init signing: " + get_openssl_error()));
    }

    // Set RSA-PSS padding with salt length = digest length (32 bytes for SHA-256)
    if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(Error::signing("Failed to set padding: " + get_openssl_error()));
    }

    if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(Error::signing("Failed to set salt length: " + get_openssl_error()));
    }

    // Update with message
    if (EVP_DigestSignUpdate(ctx, message.data(), message.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(Error::signing("Failed to update digest: " + get_openssl_error()));
    }

    // Get signature length
    size_t sig_len = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(
            Error::signing("Failed to get signature length: " + get_openssl_error()));
    }

    // Sign
    std::vector<unsigned char> signature(sig_len);
    if (EVP_DigestSignFinal(ctx, signature.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::unexpected(Error::signing("Failed to sign: " + get_openssl_error()));
    }

    EVP_MD_CTX_free(ctx);

    return AuthHeaders{.access_key = impl_->api_key_id,
                       .signature = base64_encode(signature.data(), sig_len),
                       .timestamp = timestamp_str};
}

} // namespace kalshi
