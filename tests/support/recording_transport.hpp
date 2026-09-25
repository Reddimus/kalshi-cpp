#pragma once

#include "kalshi/http_client.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kalshi::test {

/// Records every request and answers with a scripted response.
class RecordingTransport final : public HttpTransport {
public:
	int status{200};
	std::string response_body{"{}"};

	mutable int calls{0};
	mutable HttpMethod method{HttpMethod::GET};
	mutable std::string target;
	mutable std::string body;

	[[nodiscard]] Result<HttpResponse> request(HttpMethod request_method, std::string_view path,
											   std::string_view request_body) const override {
		++calls;
		method = request_method;
		target = path;
		body = request_body;
		return HttpResponse{status, response_body, {}};
	}
};

} // namespace kalshi::test
