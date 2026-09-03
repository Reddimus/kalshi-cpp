/// @file ws_json.hpp
/// @brief Internal JSON helpers used by the WebSocket parser.
///
/// The WebSocket message handler needs a very small, allocation-free
/// JSON reader: only extract_int / extract_string / extract_orderbook_entries
/// are used, and only from a handful of call sites. A full JSON library
/// would pull in more dependencies and perform worse on hot-path messages.
///
/// The helpers live here (rather than inline in websocket.cpp) so the
/// unit tests can exercise them directly. They are in the ``detail``
/// namespace and should not be considered part of the public API.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kalshi::detail {

/// Find an exact JSON object key without allocating a quoted search string.
/// This intentionally keeps the parser's existing find-first behavior while
/// rejecting matching string values that are not followed by a colon.
inline std::size_t find_json_key(std::string_view json, std::string_view key) noexcept {
	std::size_t pos = 0;
	while ((pos = json.find('"', pos)) != std::string_view::npos) {
		const std::size_t key_start = pos + 1;
		const std::size_t key_end = key_start + key.size();
		if (key_end < json.size() && json.substr(key_start, key.size()) == key &&
			json[key_end] == '"') {
			const std::size_t colon = json.find_first_not_of(" \t\r\n", key_end + 1);
			if (colon != std::string_view::npos && json[colon] == ':')
				return pos;
		}
		++pos;
	}
	return std::string_view::npos;
}

/// Extract an integer value for ``key`` from ``json``.
///
/// Tolerates both raw JSON numbers (``"x":47``) and JSON-string numbers
/// (``"x":"47"``). Kalshi's v2 WebSocket schema encodes trade / orderbook
/// price and count fields as strings; earlier revisions of this parser
/// treated the leading ``"`` as end-of-number and silently returned 0,
/// zeroing out every production tick. Returns 0 when the key is absent
/// or the value cannot be parsed.
inline std::int32_t extract_int(std::string_view json, std::string_view key) {
	std::size_t pos = find_json_key(json, key);
	if (pos == std::string_view::npos)
		return 0;
	pos = json.find(':', pos);
	if (pos == std::string::npos)
		return 0;
	pos++;
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		pos++;
	const bool quoted = pos < json.size() && json[pos] == '"';
	if (quoted)
		pos++;
	bool negative = false;
	if (pos < json.size() && json[pos] == '-') {
		negative = true;
		pos++;
	}
	// Accumulate in int64 and clamp, mirroring extract_dollar_cents: a bare
	// int32 accumulator silently overflows (UB) on an out-of-range numeric
	// field from a malformed/hostile WS frame. Saturate the magnitude so int64
	// itself never overflows, then clamp to the int32 range.
	std::int64_t val = 0;
	while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
		val = val * 10 + (json[pos] - '0');
		if (val > 2147483648LL) { // |INT32_MIN| — largest magnitude we keep
			val = 2147483648LL;
		}
		pos++;
	}
	const std::int64_t result = negative ? -val : val;
	if (result < INT32_MIN) {
		return INT32_MIN;
	}
	if (result > INT32_MAX) {
		return INT32_MAX;
	}
	return static_cast<std::int32_t>(result);
}

inline std::int64_t extract_int64(std::string_view json, std::string_view key) {
	std::size_t pos = find_json_key(json, key);
	if (pos == std::string_view::npos)
		return 0;
	pos = json.find(':', pos + key.size() + 2);
	if (pos == std::string::npos)
		return 0;
	pos = json.find_first_not_of(" \t\r\n", pos + 1);
	if (pos == std::string::npos)
		return 0;
	if (json[pos] == '"')
		++pos;
	bool negative = false;
	if (pos < json.size() && json[pos] == '-') {
		negative = true;
		++pos;
	}
	std::uint64_t value = 0;
	const std::uint64_t limit = negative ? std::uint64_t{INT64_MAX} + 1U : std::uint64_t{INT64_MAX};
	bool saw_digit = false;
	while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
		saw_digit = true;
		const std::uint64_t digit = static_cast<std::uint64_t>(json[pos] - '0');
		if (value > (limit - digit) / 10U)
			return 0;
		value = value * 10U + digit;
		++pos;
	}
	if (!saw_digit)
		return 0;
	if (negative && value == std::uint64_t{INT64_MAX} + 1U)
		return INT64_MIN;
	const std::int64_t signed_value = static_cast<std::int64_t>(value);
	return negative ? -signed_value : signed_value;
}

/// Extract a string value for ``key`` from ``json``. Returns "" when
/// the key is absent or the value is not a string.
inline std::string extract_string(std::string_view json, std::string_view key) {
	std::size_t pos = find_json_key(json, key);
	if (pos == std::string_view::npos)
		return "";
	pos = json.find(':', pos);
	if (pos == std::string::npos)
		return "";
	pos = json.find('"', pos);
	if (pos == std::string::npos)
		return "";
	const std::size_t start = pos + 1;
	const std::size_t end = json.find('"', start);
	if (end == std::string::npos)
		return "";
	return std::string{json.substr(start, end - start)};
}

/// Extract a JSON boolean. Missing keys, strings, and malformed tokens are false.
inline bool extract_bool(std::string_view json, std::string_view key) {
	std::size_t pos = find_json_key(json, key);
	if (pos == std::string_view::npos)
		return false;
	pos = json.find(':', pos + key.size() + 2);
	if (pos == std::string::npos)
		return false;
	pos = json.find_first_not_of(" \t\r\n", pos + 1);
	if (pos == std::string::npos || json.compare(pos, 4, "true") != 0)
		return false;
	const std::size_t end = pos + 4;
	return end == json.size() || json[end] == ',' || json[end] == '}' || json[end] == ']' ||
		   json[end] == ' ' || json[end] == '\t' || json[end] == '\r' || json[end] == '\n';
}

/// Parse a decimal-dollar JSON-string value into integer cents.
///
/// Kalshi's v2 WebSocket schema encodes price fields as decimal
/// dollar strings with 4 fractional digits, e.g. ``"yes_price_dollars":"0.3200"``
/// means 32 cents. The SDK's public structs keep cents as
/// ``std::int32_t`` — this helper does the unit conversion at parse
/// time so callers never see the dollar representation.
///
/// The parser is deliberately simple-minded (no ``atof``, no locale):
/// it reads integer and fractional parts as decimal digits and sums
/// ``integer * 100 + round(fractional_digits_1..2)``. Digits past
/// the second fractional position round-half-up the cent. Out-of-range
/// or malformed input returns 0.
inline std::int32_t extract_dollar_cents(std::string_view json, std::string_view key) {
	const std::string s = extract_string(json, key);
	if (s.empty())
		return 0;

	std::size_t i = 0;
	bool negative = false;
	if (i < s.size() && s[i] == '-') {
		negative = true;
		i++;
	}
	std::int64_t whole = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		whole = whole * 10 + (s[i] - '0');
		i++;
	}
	std::int32_t cents_frac = 0;
	if (i < s.size() && s[i] == '.') {
		i++;
		// Read up to two fractional digits as cents.
		for (int d = 0; d < 2; d++) {
			cents_frac *= 10;
			if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
				cents_frac += s[i] - '0';
				i++;
			}
		}
		// Round on the third fractional digit if present.
		if (i < s.size() && s[i] >= '5' && s[i] <= '9') {
			cents_frac++;
		}
	}
	const std::int64_t total = whole * 100 + cents_frac;
	const std::int64_t signed_total = negative ? -total : total;
	// Clamp into int32 range defensively; Kalshi prices are always in
	// [0, 100] cents so the cast is safe in practice.
	if (signed_total > 2147483647LL)
		return 2147483647;
	if (signed_total < -2147483648LL)
		return -2147483648;
	return static_cast<std::int32_t>(signed_total);
}

/// Parse a floating-point-count JSON-string value into a rounded integer.
///
/// Kalshi's v2 schema delivers ``count_fp`` and orderbook ``delta_fp``
/// as string-encoded floats (``"40.00"``, ``"-30.87"``). The SDK's
/// public structs keep counts as ``std::int32_t`` — this rounds to
/// the nearest integer and preserves sign. Malformed input → 0.
inline std::int32_t extract_fp_int(std::string_view json, std::string_view key) {
	const std::string s = extract_string(json, key);
	if (s.empty())
		return 0;

	std::size_t i = 0;
	bool negative = false;
	if (i < s.size() && s[i] == '-') {
		negative = true;
		i++;
	}
	std::int64_t whole = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		whole = whole * 10 + (s[i] - '0');
		i++;
	}
	// Round based on the first fractional digit if present.
	if (i < s.size() && s[i] == '.') {
		i++;
		if (i < s.size() && s[i] >= '5' && s[i] <= '9') {
			whole++;
		}
	}
	const std::int64_t signed_total = negative ? -whole : whole;
	if (signed_total > 2147483647LL)
		return 2147483647;
	if (signed_total < -2147483648LL)
		return -2147483648;
	return static_cast<std::int32_t>(signed_total);
}

/// Parse an ISO-8601 timestamp string (``2026-04-20T08:19:13.898402Z``)
/// into Unix milliseconds. Returns 0 on parse failure.
///
/// Only the ``YYYY-MM-DDTHH:MM:SS[.fraction]Z`` shape is supported —
/// Kalshi sends UTC timestamps with an optional fractional component
/// and a trailing ``Z``. Accepts up to microsecond precision; truncates
/// below millisecond.
inline std::int64_t extract_iso8601_millis(std::string_view json, std::string_view key) {
	const std::string s = extract_string(json, key);
	if (s.size() < 19) // min "YYYY-MM-DDTHH:MM:SS"
		return 0;

	auto read_uint = [](std::string_view str, std::size_t pos, int n) -> int {
		int v = 0;
		for (int i = 0; i < n && pos + i < str.size(); i++) {
			char c = str[pos + i];
			if (c < '0' || c > '9')
				return -1;
			v = v * 10 + (c - '0');
		}
		return v;
	};

	const int year = read_uint(s, 0, 4);
	const int month = read_uint(s, 5, 2);
	const int day = read_uint(s, 8, 2);
	const int hour = read_uint(s, 11, 2);
	const int minute = read_uint(s, 14, 2);
	const int second = read_uint(s, 17, 2);
	if (year < 0 || month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0)
		return 0;

	// Days-from-civil algorithm (Howard Hinnant), returns days from
	// 1970-01-01. Correct for [-32'767, +32'767] year range.
	int y = year;
	const int m = month;
	const int d = day;
	if (m <= 2)
		y -= 1;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const int yoe = y - era * 400;
	const int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;

	std::int64_t seconds = days * 86400 + static_cast<std::int64_t>(hour) * 3600 +
						   static_cast<std::int64_t>(minute) * 60 + second;

	// Fractional part: read up to 3 digits (milliseconds).
	int millis = 0;
	if (19 < s.size() && s[19] == '.') {
		std::size_t i = 20;
		for (int d2 = 0; d2 < 3; d2++) {
			millis *= 10;
			if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
				millis += s[i] - '0';
				i++;
			}
		}
	}
	return seconds * 1000 + millis;
}

/// Result of a single orderbook-array entry.
struct PriceQty {
	std::int32_t price{0};
	std::int32_t quantity{0};
	std::string price_fp;
	std::string quantity_fp;
};

/// Extract a list of ``[price, quantity]`` tuples from
/// ``json[key]``. Supports both raw-number and JSON-string encodings
/// for either slot, mirroring ``extract_int``'s quote tolerance.
/// Returns an empty vector when the key is absent or malformed.
inline std::vector<PriceQty> extract_orderbook_entries(std::string_view json,
													   std::string_view key) {
	std::vector<PriceQty> entries;
	std::size_t pos = find_json_key(json, key);
	if (pos == std::string_view::npos)
		return entries;
	pos = json.find('[', pos);
	if (pos == std::string::npos)
		return entries;
	pos++; // Skip outer '['

	auto read_num = [&](std::string& raw) -> std::int32_t {
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
			pos++;
		const bool quoted = pos < json.size() && json[pos] == '"';
		if (quoted)
			pos++;
		const std::size_t start = pos;
		// int64 accumulate + saturate: orderbook price/size fields are
		// non-negative, but a malformed frame could carry an out-of-int32 value
		// that would overflow a bare int32 accumulator (UB).
		std::int64_t val = 0;
		while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
			val = val * 10 + (json[pos] - '0');
			if (val > INT32_MAX) {
				val = INT32_MAX;
			}
			pos++;
		}
		if (pos < json.size() && json[pos] == '.') {
			++pos;
			while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
				++pos;
		}
		raw = json.substr(start, pos - start);
		if (quoted && pos < json.size() && json[pos] == '"')
			pos++;
		return static_cast<std::int32_t>(val);
	};

	while (pos < json.size()) {
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
			pos++;
		if (pos >= json.size() || json[pos] == ']')
			break;

		if (json[pos] == '[') {
			pos++;
			std::string price_fp;
			std::string quantity_fp;
			const std::int32_t price = read_num(price_fp);
			while (pos < json.size() && json[pos] != ',' && json[pos] != ']')
				pos++;
			if (pos < json.size() && json[pos] == ',')
				pos++;
			const std::int32_t qty = read_num(quantity_fp);
			while (pos < json.size() && json[pos] != ']')
				pos++;
			if (pos < json.size())
				pos++; // Skip ']'
			entries.push_back(PriceQty{price, qty, std::move(price_fp), std::move(quantity_fp)});
		}

		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ','))
			pos++;
	}
	return entries;
}

} // namespace kalshi::detail
