#include "kalshi/http_client.hpp"

#include <curl/curl.h>
#include <sstream>

namespace kalshi {

struct HttpClient::Impl {
	Signer signer;
	ClientConfig config;
	CURL* curl{nullptr};

	Impl(Signer s, ClientConfig c) : signer(std::move(s)), config(std::move(c)) {
		curl = curl_easy_init();
	}

	~Impl() {
		if (curl) {
			curl_easy_cleanup(curl);
		}
	}
};

namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	std::string* response = static_cast<std::string*>(userdata);
	response->append(ptr, size * nmemb);
	return size * nmemb;
}

size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
	auto* headers = static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
	std::string line(buffer, size * nitems);

	std::size_t colon = line.find(':');
	if (colon != std::string::npos) {
		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		// Trim whitespace
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
			value.erase(0, 1);
		}
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
			value.pop_back();
		}
		headers->emplace_back(std::move(key), std::move(value));
	}
	return size * nitems;
}

} // namespace

HttpClient::HttpClient(Signer signer, ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(signer), std::move(config))) {}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept = default;

HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

const ClientConfig& HttpClient::config() const noexcept {
	return impl_->config;
}

Result<HttpResponse> HttpClient::get(std::string_view path) const {
	return request(HttpMethod::GET, path);
}

Result<HttpResponse> HttpClient::post(std::string_view path, std::string_view body) const {
	return request(HttpMethod::POST, path, body);
}

Result<HttpResponse> HttpClient::put(std::string_view path, std::string_view body) const {
	return request(HttpMethod::PUT, path, body);
}

Result<HttpResponse> HttpClient::del(std::string_view path) const {
	return request(HttpMethod::DELETE, path);
}

Result<HttpResponse> HttpClient::request(HttpMethod method, std::string_view path,
										 std::string_view body) const {
	if (!impl_->curl) {
		return std::unexpected(Error::network("CURL not initialized"));
	}

	// Sign the request
	Result<AuthHeaders> headers_result =
		impl_->signer.sign(std::string(to_string(method)), std::string(path));
	if (!headers_result) {
		return std::unexpected(headers_result.error());
	}
	const AuthHeaders& auth = *headers_result;

	// Build URL
	std::string url = impl_->config.base_url + std::string(path);

	// Reset curl handle
	curl_easy_reset(impl_->curl);

	// Set URL
	curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());

	// Set method
	switch (method) {
		case HttpMethod::GET:
			curl_easy_setopt(impl_->curl, CURLOPT_HTTPGET, 1L);
			break;
		case HttpMethod::POST:
			curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
			break;
		case HttpMethod::PUT:
			curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "PUT");
			break;
		case HttpMethod::DELETE:
			curl_easy_setopt(impl_->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
			break;
	}

	// Set body if present
	if (!body.empty()) {
		curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, body.data());
		curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	}

	// Build headers
	curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, ("KALSHI-ACCESS-KEY: " + auth.access_key).c_str());
	headers = curl_slist_append(headers, ("KALSHI-ACCESS-SIGNATURE: " + auth.signature).c_str());
	headers = curl_slist_append(headers, ("KALSHI-ACCESS-TIMESTAMP: " + auth.timestamp).c_str());
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "Accept: application/json");
	curl_easy_setopt(impl_->curl, CURLOPT_HTTPHEADER, headers);

	// Set timeout
	curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT, impl_->config.timeout.count());

	// SSL verification
	curl_easy_setopt(impl_->curl, CURLOPT_SSL_VERIFYPEER, impl_->config.verify_ssl ? 1L : 0L);

	// Response handling
	HttpResponse response;
	curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(impl_->curl, CURLOPT_HEADERFUNCTION, header_callback);
	curl_easy_setopt(impl_->curl, CURLOPT_HEADERDATA, &response.headers);

	// Perform request
	CURLcode res = curl_easy_perform(impl_->curl);

	// Cleanup headers
	curl_slist_free_all(headers);

	if (res != CURLE_OK) {
		return std::unexpected(Error::network(curl_easy_strerror(res)));
	}

	// Get status code
	long status_code = 0;
	curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &status_code);
	response.status_code = static_cast<std::int16_t>(status_code);

	return response;
}

} // namespace kalshi
