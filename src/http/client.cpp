#include "kalshi/detail/http_path.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <curl/curl.h>
#include <mutex>
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

std::size_t write_body(char* data, std::size_t size, std::size_t count, void* userdata) {
	static_cast<std::string*>(userdata)->append(data, size * count);
	return size * count;
}

std::size_t write_header(char* data, std::size_t size, std::size_t count, void* userdata) {
	std::vector<std::pair<std::string, std::string>>& headers =
		*static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
	const std::string_view line{data, size * count};
	// A new status line starts a new response (after redirects or 100 Continue).
	if (line.starts_with("HTTP/")) {
		headers.clear();
		return size * count;
	}
	const std::size_t colon = line.find(':');
	if (colon != std::string_view::npos) {
		std::string_view value = line.substr(colon + 1);
		const std::size_t first = value.find_first_not_of(" \t");
		const std::size_t last = value.find_last_not_of(" \t\r\n");
		value = first == std::string_view::npos ? std::string_view{}
												: value.substr(first, last - first + 1);
		headers.emplace_back(std::string(line.substr(0, colon)), std::string(value));
	}
	return size * count;
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

	curl_slist* raw_headers = nullptr;
	const auto append_header = [&raw_headers](const std::string& header) {
		raw_headers = curl_slist_append(raw_headers, header.c_str());
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
	const std::unique_ptr<curl_slist, SlistDeleter> headers{raw_headers};

	const std::string url = detail::request_url(impl_->config.base_url, path);
	HttpResponse response;

	// Reset clears per-request options but keeps pooled connections and the
	// TLS session cache.
	curl_easy_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, to_curl_millis(impl_->config.timeout));
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
					 to_curl_millis(impl_->config.connect_timeout));
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // every encoding libcurl supports
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, impl_->config.verify_ssl ? 1L : 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, impl_->config.verify_ssl ? 2L : 0L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

	if (method == HttpMethod::GET) {
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	} else {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, to_string(method).data());
	}
	// Always pass the body explicitly, even when empty: a POST without
	// POSTFIELDS makes libcurl read the request body from stdin.
	if (sends_body) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
	}

	const CURLcode result = curl_easy_perform(curl);
	if (result != CURLE_OK) {
		return std::unexpected(Error::network(curl_easy_strerror(result)));
	}
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	response.status_code = static_cast<int>(status);
	return response;
}

} // namespace kalshi
