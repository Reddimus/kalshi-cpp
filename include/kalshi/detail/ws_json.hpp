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
