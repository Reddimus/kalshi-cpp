#pragma once

// A throwaway certificate authority for TLS tests. Nothing here is written to
// disk; a test saves the PEM it needs into its own scratch directory.

#include <cstdio>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <string>

namespace kalshi::test {

/// A certificate authority that exists for one test.
class TestCertificateAuthority {
public:
	/// A certificate with its private key, both as PEM.
	struct Issued {
		std::string certificate;
		std::string private_key;
	};

	TestCertificateAuthority() : key_(new_key()) {
		// A random name, so two test runs never share a hashed file name.
		unsigned char suffix[8]{};
		RAND_bytes(suffix, sizeof(suffix));
		std::string name = "kalshi-cpp test CA ";
		for (const unsigned char byte : suffix) {
			name += "0123456789abcdef"[byte >> 4];
			name += "0123456789abcdef"[byte & 0x0F];
		}
		certificate_ = build(key_, name, nullptr, key_, {});
	}
	~TestCertificateAuthority() {
		X509_free(certificate_);
		EVP_PKEY_free(key_);
	}
	TestCertificateAuthority(const TestCertificateAuthority&) = delete;
	TestCertificateAuthority& operator=(const TestCertificateAuthority&) = delete;

	/// The authority's own certificate, which a client must trust.
	[[nodiscard]] std::string certificate() const { return pem(certificate_); }

	/// The name OpenSSL looks for in a CA directory: the subject hash and ".0".
	[[nodiscard]] std::string hashed_file_name() const {
		char name[32]{};
		std::snprintf(name, sizeof(name), "%08lx.0", X509_subject_name_hash(certificate_));
		return name;
	}

	/// Issues a server certificate. `subject_alt_name` uses OpenSSL's syntax,
	/// such as "DNS:localhost,IP:127.0.0.1".
	[[nodiscard]] Issued issue(const std::string& subject_alt_name) const {
		EVP_PKEY* key = new_key();
		X509* certificate =
			build(key, "kalshi-cpp test server", certificate_, key_, subject_alt_name);
		Issued issued{pem(certificate), pem(key)};
		X509_free(certificate);
		EVP_PKEY_free(key);
		return issued;
	}

private:
	static EVP_PKEY* new_key() {
		EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
		if (key == nullptr) {
			throw std::runtime_error("Failed to create a test key");
		}
		return key;
	}

	static void extend(X509* certificate, X509* issuer, int nid, const std::string& value) {
		X509V3_CTX context;
		X509V3_set_ctx_nodb(&context);
		X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
		X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str());
		const bool added = extension != nullptr && X509_add_ext(certificate, extension, -1) == 1;
		X509_EXTENSION_free(extension);
		if (!added) {
			throw std::runtime_error("Failed to add a test certificate extension");
		}
	}

	/// A certificate for `key` signed by `issuer_key`. A null `issuer` makes a
	/// self-signed authority; an empty `subject_alt_name` does too.
	static X509* build(EVP_PKEY* key, const std::string& common_name, X509* issuer,
					   EVP_PKEY* issuer_key, const std::string& subject_alt_name) {
		X509* certificate = X509_new();
		if (certificate == nullptr) {
			throw std::runtime_error("Failed to create a test certificate");
		}
		X509_set_version(certificate, 2); // X.509 v3
		unsigned char serial[8]{};
		RAND_bytes(serial, sizeof(serial));
		serial[0] &= 0x7F;
		BIGNUM* number = BN_bin2bn(serial, sizeof(serial), nullptr);
		BN_to_ASN1_INTEGER(number, X509_get_serialNumber(certificate));
		BN_free(number);
		X509_gmtime_adj(X509_getm_notBefore(certificate), -3600);
		X509_gmtime_adj(X509_getm_notAfter(certificate), 24 * 3600);
		X509_set_pubkey(certificate, key);
		// Built apart from the certificate: OpenSSL 4 hands out its names as const.
		X509_NAME* subject = X509_NAME_new();
		X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
								   reinterpret_cast<const unsigned char*>(common_name.c_str()), -1,
								   -1, 0);
		const bool authority = issuer == nullptr;
		X509_set_subject_name(certificate, subject);
		X509_set_issuer_name(certificate, authority ? subject : X509_get_subject_name(issuer));
		X509_NAME_free(subject);
		X509* signer = authority ? certificate : issuer;
		extend(certificate, signer, NID_basic_constraints,
			   authority ? "critical,CA:TRUE" : "critical,CA:FALSE");
		extend(certificate, signer, NID_key_usage,
			   authority ? "critical,keyCertSign,cRLSign" : "critical,digitalSignature");
		extend(certificate, signer, NID_subject_key_identifier, "hash");
		if (!authority) {
			extend(certificate, signer, NID_authority_key_identifier, "keyid");
			extend(certificate, signer, NID_ext_key_usage, "serverAuth");
			extend(certificate, signer, NID_subject_alt_name, subject_alt_name);
		}
		if (X509_sign(certificate, issuer_key, EVP_sha256()) == 0) {
			X509_free(certificate);
			throw std::runtime_error("Failed to sign a test certificate");
		}
		return certificate;
	}

	template <typename Write>
	static std::string encode(Write write) {
		BIO* output = BIO_new(BIO_s_mem());
		if (output == nullptr || !write(output)) {
			BIO_free(output);
			throw std::runtime_error("Failed to encode test TLS material");
		}
		char* data = nullptr;
		const long size = BIO_get_mem_data(output, &data);
		std::string text(data, static_cast<std::size_t>(size));
		BIO_free(output);
		return text;
	}

	static std::string pem(X509* certificate) {
		return encode(
			[certificate](BIO* output) { return PEM_write_bio_X509(output, certificate) == 1; });
	}

	static std::string pem(EVP_PKEY* key) {
		return encode([key](BIO* output) {
			return PEM_write_bio_PrivateKey(output, key, nullptr, nullptr, 0, nullptr, nullptr) ==
				   1;
		});
	}

	EVP_PKEY* key_;
	X509* certificate_{nullptr};
};

} // namespace kalshi::test
