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

std::string strip_null_members(std::string_view json) {
	struct Frame {
		bool object{false};
		bool expect_key{false};
		std::size_t kept{0};
	};
	std::vector<Frame> frames;
	std::string out;
	out.reserve(json.size());
	std::size_t i = 0;

	const auto skip_space = [&] { // auto-ok: lambda
		while (i < json.size() &&
			   (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
			++i;
		}
	};
	// Returns the string token starting at json[i] (a quote) and advances past it.
	const auto string_token = [&] { // auto-ok: lambda
		const std::size_t start = i++;
		while (i < json.size() && json[i] != '"') {
			i += json[i] == '\\' ? 2 : 1;
		}
		i = std::min(i + 1, json.size());
		return json.substr(start, i - start);
	};
	// After a value ends inside an object, the next token is a key.
	const auto value_done = [&] { // auto-ok: lambda
		if (!frames.empty() && frames.back().object) {
			frames.back().expect_key = true;
			++frames.back().kept;
		}
	};

	while (i < json.size()) {
		skip_space();
		if (i >= json.size()) {
			break;
		}
		const char ch = json[i];
		if (!frames.empty() && frames.back().object && frames.back().expect_key && ch == '"') {
			const std::string_view key = string_token();
			skip_space();
			if (i < json.size() && json[i] == ':') {
				++i;
			}
			skip_space();
			if (json.substr(i, 4) == "null") {
				const std::size_t after = i + 4;
				std::size_t next = after;
				while (next < json.size() && (json[next] == ' ' || json[next] == '\t' ||
											  json[next] == '\n' || json[next] == '\r')) {
					++next;
				}
				if (next >= json.size() || json[next] == ',' || json[next] == '}') {
					i = after;
					continue;
				}
			}
			if (frames.back().kept > 0) {
				out.push_back(',');
			}
			out.append(key).push_back(':');
			frames.back().expect_key = false;
			continue;
		}
		switch (ch) {
			case '{':
				out.push_back('{');
				frames.push_back({.object = true, .expect_key = true, .kept = 0});
				++i;
				break;
			case '[':
				out.push_back('[');
				frames.push_back({.object = false, .expect_key = false, .kept = 0});
				++i;
				break;
			case '}':
			case ']':
				out.push_back(ch);
				if (!frames.empty()) {
					frames.pop_back();
				}
				++i;
				value_done();
				break;
			case ',':
				// Objects re-emit their own commas so dropped members leave none behind.
				if (frames.empty() || !frames.back().object) {
					out.push_back(',');
				}
				++i;
				break;
			case '"':
				out.append(string_token());
				value_done();
				break;
			default: {
				const std::size_t start = i;
				while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']' &&
					   json[i] != ' ' && json[i] != '\t' && json[i] != '\n' && json[i] != '\r') {
					++i;
				}
				out.append(json.substr(start, i - start));
				value_done();
				break;
			}
		}
	}
	return out;
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
	if (status == 400 || status == 409 || status == 422) {
		code = ErrorCode::InvalidRequest;
	} else if (status == 401 || status == 403) {
		code = ErrorCode::AuthenticationError;
	} else if (status == 404) {
		code = ErrorCode::NotFound;
	} else if (status == 429) {
		code = ErrorCode::RateLimited;
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
