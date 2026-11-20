#pragma once

#include <string>
#include <string_view>

namespace kalshi::detail {

/// Join a configured API base URL and an endpoint with exactly one slash.
[[nodiscard]] inline std::string request_url(std::string_view base_url, std::string_view endpoint) {
	std::string result{base_url};
	if (!result.empty() && result.back() == '/' && !endpoint.empty() && endpoint.front() == '/') {
		result.pop_back();
	} else if (!result.empty() && result.back() != '/' && !endpoint.empty() &&
			   endpoint.front() != '/') {
		result.push_back('/');
	}
	result.append(endpoint);
	return result;
}

/// Whether a base URL carries a query or a fragment. An endpoint cannot be
/// joined to such a URL: it would land inside the query or the fragment.
[[nodiscard]] constexpr bool has_query_or_fragment(std::string_view base_url) noexcept {
	return base_url.find_first_of("?#") != std::string_view::npos;
}

/// Build the URL path covered by Kalshi's request signature.
///
/// Kalshi signs the full path from the host root, including the base URL's
/// `/trade-api/v2` prefix, but excludes the query string. A fragment is never
/// sent, so it is excluded too.
[[nodiscard]] inline std::string request_signing_path(std::string_view base_url,
													  std::string_view endpoint) {
	// The authority ends at the first `/`, `?`, or `#`, so a slash inside a
	// query or fragment never starts the path. `://` marks a scheme only when
	// it comes before all three.
	const std::size_t scheme = base_url.find("://");
	const bool has_scheme =
		scheme != std::string_view::npos && base_url.find_first_of("/?#") == scheme + 1;
	std::string_view base_path = base_url.substr(has_scheme ? scheme + 3 : 0);
	const std::size_t path_start = base_path.find_first_of("/?#");
	base_path =
		path_start == std::string_view::npos ? std::string_view{} : base_path.substr(path_start);
	base_path = base_path.substr(0, base_path.find_first_of("?#"));
	std::string result = base_path.empty() ? std::string{"/"} : std::string{base_path};

	endpoint = endpoint.substr(0, endpoint.find_first_of("?#"));
	if (result.back() == '/' && !endpoint.empty() && endpoint.front() == '/') {
		result.pop_back();
	} else if (result.back() != '/' && !endpoint.empty() && endpoint.front() != '/') {
		result.push_back('/');
	}
	result.append(endpoint);
	return result;
}

} // namespace kalshi::detail
