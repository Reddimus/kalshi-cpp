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
#include <utility>
#include <vector>

namespace kalshi::detail {

/// Extract an integer value for ``key`` from ``json``.
///
/// Tolerates both raw JSON numbers (``"x":47``) and JSON-string numbers
/// (``"x":"47"``). Kalshi's v2 WebSocket schema encodes trade / orderbook
/// price and count fields as strings; earlier revisions of this parser
/// treated the leading ``"`` as end-of-number and silently returned 0,
/// zeroing out every production tick. Returns 0 when the key is absent
/// or the value cannot be parsed.
inline std::int32_t extract_int(const std::string& json, const std::string& key) {
	const std::string search = "\"" + key + "\"";
	std::size_t pos = json.find(search);
	if (pos == std::string::npos)
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
	std::int32_t val = 0;
	while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
		val = val * 10 + (json[pos] - '0');
		pos++;
	}
	return negative ? -val : val;
}

/// Extract a string value for ``key`` from ``json``. Returns "" when
/// the key is absent or the value is not a string.
inline std::string extract_string(const std::string& json, const std::string& key) {
	const std::string search = "\"" + key + "\"";
	std::size_t pos = json.find(search);
	if (pos == std::string::npos)
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
	return json.substr(start, end - start);
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
inline std::int32_t extract_dollar_cents(const std::string& json, const std::string& key) {
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
inline std::int32_t extract_fp_int(const std::string& json, const std::string& key) {
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
inline std::int64_t extract_iso8601_millis(const std::string& json, const std::string& key) {
	const std::string s = extract_string(json, key);
	if (s.size() < 19) // min "YYYY-MM-DDTHH:MM:SS"
		return 0;

	auto read_uint = [](const std::string& str, std::size_t pos, int n) -> int {
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
};

/// Extract a list of ``[price, quantity]`` tuples from
/// ``json[key]``. Supports both raw-number and JSON-string encodings
/// for either slot, mirroring ``extract_int``'s quote tolerance.
/// Returns an empty vector when the key is absent or malformed.
inline std::vector<PriceQty> extract_orderbook_entries(const std::string& json,
													   const std::string& key) {
	std::vector<PriceQty> entries;
	const std::string search = "\"" + key + "\"";
	std::size_t pos = json.find(search);
	if (pos == std::string::npos)
		return entries;
	pos = json.find('[', pos);
	if (pos == std::string::npos)
		return entries;
	pos++; // Skip outer '['

	auto read_num = [&]() -> std::int32_t {
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
			pos++;
		const bool quoted = pos < json.size() && json[pos] == '"';
		if (quoted)
			pos++;
		std::int32_t val = 0;
		while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
			val = val * 10 + (json[pos] - '0');
			pos++;
		}
		if (quoted && pos < json.size() && json[pos] == '"')
			pos++;
		return val;
	};

	while (pos < json.size()) {
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
			pos++;
		if (pos >= json.size() || json[pos] == ']')
			break;

		if (json[pos] == '[') {
			pos++;
			const std::int32_t price = read_num();
			while (pos < json.size() && json[pos] != ',' && json[pos] != ']')
				pos++;
			if (pos < json.size() && json[pos] == ',')
				pos++;
			const std::int32_t qty = read_num();
			while (pos < json.size() && json[pos] != ']')
				pos++;
			if (pos < json.size())
				pos++; // Skip ']'
			entries.push_back(PriceQty{price, qty});
		}

		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ','))
			pos++;
	}
	return entries;
}

} // namespace kalshi::detail
