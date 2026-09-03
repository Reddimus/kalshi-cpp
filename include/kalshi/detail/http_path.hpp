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

/// Build the URL path covered by Kalshi's RSA-PSS signature.
///
/// Kalshi signs the full path from the host root, including the base URL's
/// `/trade-api/v2` prefix, but excludes the query string.
[[nodiscard]] inline std::string request_signing_path(std::string_view base_url,
													  std::string_view endpoint) {
	const std::size_t scheme = base_url.find("://");
	const std::size_t authority_start = scheme == std::string_view::npos ? 0 : scheme + 3;
	const std::size_t path_start = base_url.find('/', authority_start);
	std::string result = path_start == std::string_view::npos
							 ? std::string{"/"}
							 : std::string{base_url.substr(path_start)};
	const std::size_t base_query = result.find('?');
	if (base_query != std::string::npos) {
		result.resize(base_query);
	}

	const std::size_t endpoint_query = endpoint.find('?');
	endpoint = endpoint.substr(0, endpoint_query);
	if (result.empty()) {
		result.push_back('/');
	}
	if (result.back() == '/' && !endpoint.empty() && endpoint.front() == '/') {
		result.pop_back();
	} else if (result.back() != '/' && !endpoint.empty() && endpoint.front() != '/') {
		result.push_back('/');
	}
	result.append(endpoint);
	return result;
}

} // namespace kalshi::detail
