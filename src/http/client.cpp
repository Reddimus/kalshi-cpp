#include "kalshi/detail/http_path.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <curl/curl.h>
#include <limits>
#include <mutex>
#include <new>
#include <string>

namespace kalshi {

namespace {

/// curl_global_init() must run once before any easy handle exists.
CURLcode curl_global_result() {
	static const CURLcode result = [] {
		const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
		if (code == CURLE_OK) {
			std::atexit(curl_global_cleanup);
		}
		return code;
	}();
	return result;
}

struct CurlDeleter {
	void operator()(CURL* handle) const noexcept { curl_easy_cleanup(handle); }
};
struct SlistDeleter {
	void operator()(curl_slist* list) const noexcept { curl_slist_free_all(list); }
};

// Built at load time so config() stays noexcept on a moved-from client.
const ClientConfig kMovedFromConfig{};

const std::string kUserAgent = std::string("User-Agent: kalshi-cpp/") + VERSION;

struct ResponseSink {
	HttpResponse response;
	std::size_t limit;
	bool too_large{false};
};

std::size_t write_body(char* data, std::size_t size, std::size_t count, void* userdata) noexcept {
	ResponseSink& sink = *static_cast<ResponseSink*>(userdata);
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
		return 0;
	}
	const std::size_t bytes = size * count;
	if (sink.limit != 0 && bytes > sink.limit - sink.response.body.size()) {
		sink.too_large = true;
		return 0;
	}
	try {
		sink.response.body.append(data, bytes);
		return bytes;
	} catch (...) {
		return 0;
	}
}

std::size_t write_header(char* data, std::size_t size, std::size_t count, void* userdata) noexcept {
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
		return 0;
	}
	try {
		std::vector<std::pair<std::string, std::string>>& headers =
			*static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
		const std::string_view line{data, size * count};
		if (line.starts_with("HTTP/")) {
			headers.clear();
			return size * count;
		}
		const std::size_t colon = line.find(':');
		if (colon != std::string_view::npos) {
			std::string_view value = line.substr(colon + 1);
			const std::size_t first = value.find_first_not_of(" \t\r\n");
			const std::size_t last = value.find_last_not_of(" \t\r\n");
			value = first == std::string_view::npos ? std::string_view{}
													: value.substr(first, last - first + 1);
			headers.emplace_back(std::string(line.substr(0, colon)), std::string(value));
		}
		return size * count;
	} catch (...) {
		return 0;
	}
}

long to_curl_millis(std::chrono::milliseconds duration) {
	return static_cast<long>(std::clamp<std::int64_t>(duration.count(), 0, 0x7fffffff));
}

} // namespace

std::optional<std::string_view> HttpResponse::header(std::string_view name) const noexcept {
	const auto equals_ignore_case = [](std::string_view a, std::string_view b) {
		return std::ranges::equal(a, b, [](char x, char y) {
			return std::tolower(static_cast<unsigned char>(x)) ==
				   std::tolower(static_cast<unsigned char>(y));
		});
	};
	for (const std::pair<std::string, std::string>& entry : headers) {
		if (equals_ignore_case(entry.first, name)) {
			return std::string_view{entry.second};
		}
	}
	return std::nullopt;
}

struct HttpClient::Impl {
	std::optional<Signer> signer;
	ClientConfig config;
	std::unique_ptr<CURL, CurlDeleter> curl;
	CURLcode init_result{CURLE_OK};
	std::mutex mutex;

	Impl(std::optional<Signer> s, ClientConfig c)
		: signer(std::move(s)), config(std::move(c)), init_result(curl_global_result()) {
		if (init_result == CURLE_OK) {
			curl.reset(curl_easy_init());
		}
	}
};

HttpClient::HttpClient(Signer signer, ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(signer), std::move(config))) {}

HttpClient::HttpClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::nullopt, std::move(config))) {}

HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

const ClientConfig& HttpClient::config() const noexcept {
	return impl_ ? impl_->config : kMovedFromConfig;
}

Result<HttpResponse> HttpClient::request(HttpMethod method, std::string_view path,
										 std::string_view body) const {
	if (!impl_) {
		return std::unexpected(Error::network("HttpClient is empty (moved-from)"));
	}
	const std::scoped_lock lock(impl_->mutex);
	CURL* curl = impl_->curl.get();
	if (curl == nullptr) {
		return std::unexpected(Error::network(impl_->init_result == CURLE_OK
												  ? "Failed to create a libcurl handle"
												  : curl_easy_strerror(impl_->init_result)));
	}

	const std::string url = detail::request_url(impl_->config.base_url, path);
	const auto has_scheme = [&url](std::string_view prefix) {
		return url.size() >= prefix.size() &&
			   std::equal(prefix.begin(), prefix.end(), url.begin(),
						  [](char expected, char actual) {
							  const char lower = actual >= 'A' && actual <= 'Z'
													 ? static_cast<char>(actual - 'A' + 'a')
													 : actual;
							  return expected == lower;
						  });
	};
	if ((!has_scheme("https://") && !has_scheme("http://")) ||
		url.find('\0') != std::string::npos) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "Expected an HTTP or HTTPS URL without NUL bytes"});
	}
	std::unique_ptr<curl_slist, SlistDeleter> headers;
	bool headers_ok = true;
	const auto append_header = [&headers, &headers_ok](const std::string& header) {
		curl_slist* previous = headers.release();
		curl_slist* updated = curl_slist_append(previous, header.c_str());
		headers.reset(updated != nullptr ? updated : previous);
		headers_ok = headers_ok && updated != nullptr;
	};
	if (const std::optional<Signer>& signer = impl_->signer; signer.has_value()) {
		const Result<AuthHeaders> auth = signer->sign(
			to_string(method), detail::request_signing_path(impl_->config.base_url, path));
		if (!auth) {
			return std::unexpected(auth.error());
		}
		append_header("KALSHI-ACCESS-KEY: " + auth->access_key);
		append_header("KALSHI-ACCESS-SIGNATURE: " + auth->signature);
		append_header("KALSHI-ACCESS-TIMESTAMP: " + auth->timestamp);
	}
	append_header("Accept: application/json");
	const bool sends_body = method != HttpMethod::GET;
	if (sends_body) {
		append_header("Content-Type: application/json");
	}
	append_header(kUserAgent);
	if (!headers_ok) {
		return std::unexpected(Error::network("Failed to allocate HTTP headers"));
	}
	ResponseSink sink{{}, impl_->config.max_response_bytes};

	// Reset clears per-request options but keeps pooled connections and the
	// TLS session cache.
	curl_easy_reset(curl);
	CURLcode option_error = CURLE_OK;
	const auto set_option = [curl, &option_error](CURLoption option, auto value) {
		if (option_error == CURLE_OK) {
			option_error = curl_easy_setopt(curl, option, value);
		}
	};
	set_option(CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
	set_option(CURLOPT_PROTOCOLS_STR, "http,https");
#else
	set_option(CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
	set_option(CURLOPT_HTTPHEADER, headers.get());
	set_option(CURLOPT_NOSIGNAL, 1L);
	set_option(CURLOPT_TIMEOUT_MS, to_curl_millis(impl_->config.timeout));
	set_option(CURLOPT_CONNECTTIMEOUT_MS, to_curl_millis(impl_->config.connect_timeout));
	set_option(CURLOPT_TCP_KEEPALIVE, 1L);
	set_option(CURLOPT_ACCEPT_ENCODING, ""); // every encoding libcurl supports
	set_option(CURLOPT_SSL_VERIFYPEER, impl_->config.verify_ssl ? 1L : 0L);
	set_option(CURLOPT_SSL_VERIFYHOST, impl_->config.verify_ssl ? 2L : 0L);
	set_option(CURLOPT_WRITEFUNCTION, write_body);
	set_option(CURLOPT_WRITEDATA, &sink);
	set_option(CURLOPT_HEADERFUNCTION, write_header);
	set_option(CURLOPT_HEADERDATA, &sink.response.headers);

	if (method == HttpMethod::GET) {
		set_option(CURLOPT_HTTPGET, 1L);
	} else {
		set_option(CURLOPT_CUSTOMREQUEST, to_string(method).data());
	}
	// Always pass the body explicitly, even when empty: a POST without
	// POSTFIELDS makes libcurl read the request body from stdin.
	if (sends_body) {
		set_option(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
		set_option(CURLOPT_POSTFIELDS, body.data());
	}

	if (option_error != CURLE_OK) {
		return std::unexpected(Error::network(curl_easy_strerror(option_error)));
	}
	const CURLcode result = curl_easy_perform(curl);
	if (sink.too_large) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "Response body exceeds max_response_bytes"});
	}
	if (result != CURLE_OK) {
		return std::unexpected(Error::network(curl_easy_strerror(result)));
	}
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	sink.response.status_code = static_cast<int>(status);
	return std::move(sink.response);
}

} // namespace kalshi
