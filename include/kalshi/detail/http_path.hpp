#pragma once

#include <cstddef>
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

/// Whether a base URL starts with `http://` or `https://`, in any case, and
/// names a host. The host is what is left of the authority, which ends at the
/// first `/`, `?`, or `#`, after dropping the user information up to the last
/// `@` and any `:port`. A bracketed IPv6 literal must be closed, and only a
/// port may follow it.
[[nodiscard]] constexpr bool names_http_host(std::string_view base_url) noexcept {
	// The URL after `prefix`, matched in any case, or a view with no data.
	const auto after_scheme = [base_url](std::string_view prefix) noexcept {
		if (base_url.size() < prefix.size()) {
			return std::string_view{};
		}
		std::size_t index = 0;
		for (const char expected : prefix) {
			const char actual = base_url[index++];
			const char lower =
				actual >= 'A' && actual <= 'Z' ? static_cast<char>(actual - 'A' + 'a') : actual;
			if (lower != expected) {
				return std::string_view{};
			}
		}
		std::string_view rest = base_url;
		rest.remove_prefix(prefix.size());
		return rest;
	};
	std::string_view authority = after_scheme("https://");
	if (authority.data() == nullptr) {
		authority = after_scheme("http://");
		if (authority.data() == nullptr) {
			return false;
		}
	}
	if (const std::size_t end = authority.find_first_of("/?#"); end != std::string_view::npos) {
		authority.remove_suffix(authority.size() - end);
	}
	if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
		authority.remove_prefix(at + 1);
	}
	if (!authority.empty() && authority.front() == '[') {
		const std::size_t close = authority.find(']');
		return close != std::string_view::npos && close > 1 &&
			   (close + 1 == authority.size() || authority[close + 1] == ':');
	}
	return !authority.empty() && authority.front() != ':';
}

/// Whether a URL path, up to any `?` or `#`, has a segment that is exactly `.`
/// or `..`, counting `%2e` in either case as a dot. libcurl removes such
/// segments before sending, and newer libcurl versions decode `%2e` first, so
/// the request would reach a path other than the one given and signed.
[[nodiscard]] constexpr bool has_dot_segment(std::string_view path) noexcept {
	if (const std::size_t end = path.find_first_of("?#"); end != std::string_view::npos) {
		path.remove_suffix(path.size() - end);
	}
	while (true) {
		const std::size_t slash = path.find('/');
		std::string_view segment = path;
		if (slash != std::string_view::npos) {
			segment.remove_suffix(segment.size() - slash);
		}
		std::size_t dots = 0;
		while (!segment.empty()) {
			if (segment.front() == '.') {
				segment.remove_prefix(1);
			} else if (segment.size() >= 3 && segment[0] == '%' && segment[1] == '2' &&
					   (segment[2] == 'e' || segment[2] == 'E')) {
				segment.remove_prefix(3);
			} else {
				break;
			}
			++dots;
		}
		if (segment.empty() && (dots == 1 || dots == 2)) {
			return true;
		}
		if (slash == std::string_view::npos) {
			return false;
		}
		path.remove_prefix(slash + 1);
	}
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
