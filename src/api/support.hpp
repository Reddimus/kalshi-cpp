#pragma once

// Shared plumbing for the generated operations in src/api/operations/.

#include "kalshi/api.hpp"

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "json_meta.hpp"

namespace kalshi {

struct KalshiClient::Impl {
	std::shared_ptr<const HttpTransport> transport;

	/// Sends one request and turns transport failures and non-2xx statuses
	/// into Errors, so callers only see successful responses.
	[[nodiscard]] Result<HttpResponse> send(HttpMethod method, const std::string& target,
											const std::string& body,
											std::string_view operation) const;
};

namespace detail {

/// Percent-encodes everything except RFC 3986 unreserved characters.
[[nodiscard]] std::string percent_encode(std::string_view value);

/// Fills `{name}` placeholders in order with percent-encoded values. An empty
/// value is an error because it would silently address a different route.
[[nodiscard]] Result<std::string> expand_path(std::string_view pattern,
											  std::initializer_list<std::string_view> values);

/// Builds a query string. Unset optionals and empty repeated values are left out.
class Query {
public:
	explicit Query(std::string path) : text_(std::move(path)) {}

	void add(std::string_view key, std::string_view value) { append(key, percent_encode(value)); }
	void add(std::string_view key, std::int64_t value) { append(key, std::to_string(value)); }
	void add(std::string_view key, bool value) { append(key, value ? "true" : "false"); }
	void add(std::string_view key, double value);
	/// Repeats the key for each value (`?tickers=A&tickers=B`).
	template <class T>
	void add(std::string_view key, const std::vector<T>& values) {
		for (const T& value : values) {
			add(key, value);
		}
	}
	template <class Enum>
		requires std::is_enum_v<Enum>
	void add(std::string_view key, Enum value) {
		if (const std::string_view text = to_string(value); !text.empty()) {
			add(key, text);
		}
	}
	template <class T>
	void add(std::string_view key, const std::optional<T>& value) {
		if (value) {
			add(key, *value);
		}
	}

	[[nodiscard]] std::string str() && { return std::move(text_); }

private:
	void append(std::string_view key, std::string_view encoded) {
		text_ += has_query_ ? '&' : '?';
		has_query_ = true;
		text_.append(key).append("=").append(encoded);
	}

	std::string text_;
	bool has_query_{false};
};

/// Drops object members whose value is `null`, so they read as absent. Kalshi
/// sometimes sends null for fields its spec marks non-nullable; this keeps
/// those responses parseable. Nulls inside arrays are kept.
[[nodiscard]] std::string strip_null_members(std::string_view json);

/// Maps an unsuccessful response to an Error, reading Kalshi's error body.
[[nodiscard]] Error http_error(const HttpResponse& response, std::string_view operation);

template <class T>
[[nodiscard]] std::string encode(const T& body) {
	std::string json;
	// Writing a plain aggregate cannot fail, and absent optionals are skipped.
	(void)glz::write<glz::opts{}>(body, json);
	return json;
}

template <class T>
[[nodiscard]] Result<T> decode(Result<HttpResponse> response) {
	if (!response) {
		return std::unexpected(std::move(response.error()));
	}
	constexpr glz::opts options{.error_on_unknown_keys = false};
	T value{};
	if (!glz::read<options>(value, response->body)) {
		return value;
	}
	// Kalshi sometimes sends null for fields its spec marks non-null. Retry
	// without null members only then, so well-formed bodies parse in one pass.
	const std::string json = strip_null_members(response->body);
	value = T{};
	if (const glz::error_ctx error = glz::read<options>(value, json)) {
		return std::unexpected(Error{ErrorCode::ParseError,
									 // The error names the problem but quotes none of the
									 // body, which can hold secrets (generate_api_key).
									 "Unexpected response body: " + glz::format_error(error),
									 response->status_code,
									 {}});
	}
	return value;
}

[[nodiscard]] inline Result<void> expect_success(Result<HttpResponse> response) {
	if (!response) {
		return std::unexpected(std::move(response.error()));
	}
	return {};
}

} // namespace detail
} // namespace kalshi
