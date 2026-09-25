#include "support.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace kalshi {

namespace api_detail {

// Kalshi sends {"error": {"code", "message", "details"}}, and some routes send
// {"error": "text"} (429) or a bare ErrorResponse.
struct ErrorBody {
	std::optional<RawJson> error;
	std::optional<std::string> code;
	std::optional<std::string> message;
	std::optional<std::string> details;
};

} // namespace api_detail

namespace detail {

std::string percent_encode(std::string_view value) {
	constexpr std::string_view hex = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(value.size());
	for (const char ch : value) {
		const unsigned char byte = static_cast<unsigned char>(ch);
		const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
								(byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
								byte == '_' || byte == '~';
		if (unreserved) {
			encoded.push_back(ch);
		} else {
			encoded.push_back('%');
			encoded.push_back(hex[byte >> 4]);
			encoded.push_back(hex[byte & 0x0F]);
		}
	}
	return encoded;
}

Result<std::string> expand_path(std::string_view pattern,
								std::initializer_list<std::string_view> values) {
	std::string path;
	path.reserve(pattern.size() + 32);
	const std::string_view* value = values.begin();
	while (!pattern.empty()) {
		const std::size_t open = pattern.find('{');
		path.append(pattern.substr(0, open));
		if (open == std::string_view::npos) {
			break;
		}
		const std::size_t close = pattern.find('}', open);
		const std::string_view name = pattern.substr(open + 1, close - open - 1);
		if (value == values.end() || value->empty()) {
			return std::unexpected(Error{ErrorCode::InvalidRequest,
										 "Path parameter '" + std::string(name) + "' is empty",
										 0,
										 {}});
		}
		path += percent_encode(*value++);
		pattern.remove_prefix(close + 1);
	}
	return path;
}

void Query::add(std::string_view key, double value) {
	std::array<char, 32> buffer{};
	const std::to_chars_result written =
		std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	append(key,
		   std::string_view{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())});
}

Error http_error(const HttpResponse& response, std::string_view operation) {
	const int status = response.status_code;
	ErrorCode code = ErrorCode::Unknown;
	if (status == 401 || status == 403) {
		code = ErrorCode::AuthenticationError;
	} else if (status == 404) {
		code = ErrorCode::NotFound;
	} else if (status == 408) {
		code = ErrorCode::NetworkError;
	} else if (status == 429) {
		code = ErrorCode::RateLimited;
	} else if (status >= 400 && status <= 499) {
		code = ErrorCode::InvalidRequest;
	} else if (status >= 500 && status <= 599) {
		code = ErrorCode::ServerError;
	}

	api_detail::ErrorBody body;
	constexpr glz::opts options{.error_on_unknown_keys = false};
	std::string api_code;
	std::string text;
	if (!glz::read<options>(body, response.body)) {
		if (body.error && !body.error->text.empty() && body.error->text.front() == '{') {
			api_detail::ErrorBody nested;
			if (!glz::read<options>(nested, body.error->text)) {
				body = std::move(nested);
			}
		} else if (body.error) {
			std::string plain;
			if (!glz::read_json(plain, body.error->text)) {
				text = std::move(plain);
			}
		}
		api_code = body.code.value_or("");
		if (text.empty()) {
			text = body.message.value_or("");
		}
		if (body.details && !body.details->empty()) {
			text += text.empty() ? *body.details : " (" + *body.details + ")";
		}
	}
	if (text.empty()) {
		text = response.body.substr(0, 200);
	}

	std::string message = std::string(operation) + " failed with HTTP " + std::to_string(status);
	if (!api_code.empty()) {
		message += " [" + api_code + "]";
	}
	if (!text.empty()) {
		message += ": " + text;
	}
	return Error{code, std::move(message), status, std::move(api_code)};
}

} // namespace detail

Result<HttpResponse> KalshiClient::Impl::send(HttpMethod method, const std::string& target,
											  const std::string& body,
											  std::string_view operation) const {
	Result<HttpResponse> response = transport->request(method, target, body);
	if (!response) {
		response.error().message = std::string(operation) + ": " + response.error().message;
		return response;
	}
	if (response->status_code < 200 || response->status_code > 299) {
		return std::unexpected(detail::http_error(*response, operation));
	}
	return response;
}

} // namespace kalshi
