#include "kalshi/retry.hpp"

namespace kalshi {

RetryingClient::RetryingClient(HttpClient& client, RetryPolicy policy)
	: client_(client), policy_(std::move(policy)) {}

Result<HttpResponse> RetryingClient::get(std::string_view path) {
	return with_retry([this, path]() { return client_.get(path); }, policy_);
}

Result<HttpResponse> RetryingClient::post(std::string_view path, std::string_view body) {
	return with_retry([this, path, body]() { return client_.post(path, body); }, policy_);
}

Result<HttpResponse> RetryingClient::put(std::string_view path, std::string_view body) {
	return with_retry([this, path, body]() { return client_.put(path, body); }, policy_);
}

Result<HttpResponse> RetryingClient::del(std::string_view path) {
	return with_retry([this, path]() { return client_.del(path); }, policy_);
}

const RetryPolicy& RetryingClient::policy() const noexcept {
	return policy_;
}

void RetryingClient::set_policy(RetryPolicy policy) noexcept {
	policy_ = std::move(policy);
}

} // namespace kalshi
