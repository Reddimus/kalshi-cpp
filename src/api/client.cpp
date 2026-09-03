#include "kalshi/api.hpp"
#include "kalshi/fixed_point.hpp"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "json_bodies.hpp"
#include "query_builders.hpp"
#include "response_parsers.hpp"

// ===== JSON serialization for outgoing REST request bodies =====
//
// Kalshi documents stable key order for several POST/PUT bodies (most
// importantly the order-create / amend / batch shapes). The pre-Glaze
// impl used `nlohmann::ordered_json` for the same reason. Each
// `serialize_*` member below now constructs a shim struct from
// `src/api/json_bodies.hpp`, populates it from the public params type,
// and hands it to `kalshi::ser::render_body()` which calls into
// `glz::write` with the field-order pinned by a `glz::meta`.
//
// The shim structs and their `glz::meta` specializations live in the
// non-installed `src/api/json_bodies.hpp` so they're not part of the
// public ABI — downstream consumers continue to call the unchanged
// `KalshiClient::*` methods.
//
// The corresponding REST RESPONSE parsers below use the hand-rolled
// `extract_*` scanners (kept for v2 `_dollars`/quoted-int wire-format
// handling — see CHANGELOG v0.0.8). Those are deliberately untouched.

namespace kalshi {

namespace {

// Convert a public CreateOrderParams into the shim shape used for
// serialization. Centralized so `serialize_create_order` and
// `serialize_batch_create` share the same field ordering.
ser::CreateOrderBody to_create_order_body(const CreateOrderParams& params) {
	ser::CreateOrderBody body;
	body.ticker = params.ticker;
	body.side = params.book_side ? std::string(to_json_string(*params.book_side)) : std::string{};
	body.count = params.count_fp.value_or(std::string{});
	body.price = params.price_dollars.value_or(std::string{});
	body.time_in_force = params.time_in_force.value_or(std::string{});
	body.self_trade_prevention_type = params.self_trade_prevention_type.value_or(std::string{});
	body.client_order_id = params.client_order_id;
	body.expiration_time = params.expiration_time;
	body.post_only = params.post_only;
	body.reduce_only = params.reduce_only;
	body.order_group_id = params.order_group_id;
	body.cancel_order_on_pause = params.cancel_order_on_pause;
	body.subaccount = params.subaccount;
	body.exchange_index = params.exchange_index;
	return body;
}

Result<void> validate_create_order_v2(const CreateOrderParams& params) {
	if (params.ticker.empty() || !params.book_side || !params.count_fp || !params.price_dollars ||
		!params.time_in_force || !params.self_trade_prevention_type) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest,
				  "V2 order requires ticker, book_side, count_fp, price_dollars, "
				  "time_in_force, and self_trade_prevention_type"});
	}
	if (!FixedPoint::parse(*params.count_fp) || !FixedPoint::parse(*params.price_dollars)) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "V2 order count or price is not fixed-point"});
	}
	return {};
}

ser::BatchCancelOrderBody to_batch_cancel_order_body(const BatchCancelOrder& order) {
	ser::BatchCancelOrderBody body;
	body.order_id = order.order_id;
	body.subaccount = order.subaccount;
	body.exchange_index = order.exchange_index;
	body.market_ticker = order.market_ticker;
	return body;
}

std::vector<std::string> batch_cancel_result_ids(const BatchCancelRequest& request) {
	std::vector<std::string> ids;
	if (!request.orders.empty()) {
		ids.reserve(request.orders.size());
		for (const BatchCancelOrder& order : request.orders) {
			ids.push_back(order.order_id);
		}
		return ids;
	}

	return request.order_ids;
}

} // anonymous namespace

struct KalshiClient::Impl {
	std::shared_ptr<HttpTransport> transport;
	HttpClient* http_client{nullptr};

	explicit Impl(HttpClient client) {
		const std::shared_ptr<HttpClient> owned = std::make_shared<HttpClient>(std::move(client));
		http_client = owned.get();
		transport = owned;
	}
	explicit Impl(std::shared_ptr<HttpTransport> injected) : transport(std::move(injected)) {}
};

KalshiClient::KalshiClient(HttpClient client) : impl_(std::make_unique<Impl>(std::move(client))) {}

KalshiClient::KalshiClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}

KalshiClient::~KalshiClient() = default;

KalshiClient::KalshiClient(KalshiClient&&) noexcept = default;

KalshiClient& KalshiClient::operator=(KalshiClient&&) noexcept = default;

HttpClient& KalshiClient::http_client() {
	if (impl_->http_client == nullptr) {
		throw std::logic_error("KalshiClient uses an injected transport, not HttpClient");
	}
	return *impl_->http_client;
}

const HttpClient& KalshiClient::http_client() const {
	if (impl_->http_client == nullptr) {
		throw std::logic_error("KalshiClient uses an injected transport, not HttpClient");
	}
	return *impl_->http_client;
}

HttpTransport& KalshiClient::transport() noexcept {
	return *impl_->transport;
}

const HttpTransport& KalshiClient::transport() const noexcept {
	return *impl_->transport;
}

// Simple JSON parsing helpers (minimal implementation without external dependencies)
namespace {

std::string extract_string(const std::string& json, const std::string& key) {
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos)
		return "";

	pos = json.find(':', pos);
	if (pos == std::string::npos)
		return "";

	pos = json.find('"', pos);
	if (pos == std::string::npos)
		return "";

	size_t start = pos + 1;
	// Handle escaped quotes - find unescaped closing quote
	size_t end = start;
	while (end < json.size()) {
		end = json.find('"', end);
		if (end == std::string::npos)
			return "";
		// Check if this quote is escaped (count preceding backslashes)
		size_t backslash_count = 0;
		size_t check = end;
		while (check > start && json[check - 1] == '\\') {
			backslash_count++;
			check--;
		}
		// If even number of backslashes, quote is not escaped
		if (backslash_count % 2 == 0)
			break;
		end++;
	}
	if (end == std::string::npos || end > json.size())
		return "";

	const std::string_view encoded{json.data() + pos, end - pos + 1};
	const glz::expected<std::string, glz::error_ctx> decoded =
		glz::read_json<std::string>(encoded);
	return decoded ? *decoded : std::string{};
}

std::int64_t extract_int(const std::string& json, const std::string& key) {
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos)
		return 0;

	pos = json.find(':', pos);
	if (pos == std::string::npos)
		return 0;

	// Skip whitespace
	pos++;
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		pos++;

	// Tolerate optional opening quote so quoted-number fields
	// (``"x":"47"``) parse the same as raw numbers — Kalshi's v2
	// schema string-encodes most numerics on the wire.
	bool quoted = pos < json.size() && json[pos] == '"';
	if (quoted)
		pos++;

	// Parse number with overflow protection
	std::int64_t result = 0;
	bool negative = false;
	if (pos < json.size() && json[pos] == '-') {
		negative = true;
		pos++;
	}
	constexpr std::int64_t max_safe = INT64_MAX / 10;
	while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
		int digit = json[pos] - '0';
		// Check for overflow before multiplication
		if (result > max_safe || (result == max_safe && digit > 7)) {
			return negative ? INT64_MIN : INT64_MAX;
		}
		result = result * 10 + digit;
		pos++;
	}
	return negative ? -result : result;
}

/// Parse a string-encoded decimal-dollar field (``"0.4200"``) into
/// integer cents. Returns 0 if the key is absent or empty — callers
/// that want both-shapes parsing use ``extract_cents_or_dollars``.
std::int64_t extract_dollar_cents(const std::string& json, const std::string& key) {
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
	std::int64_t cents_frac = 0;
	if (i < s.size() && s[i] == '.') {
		i++;
		for (int d = 0; d < 2; d++) {
			cents_frac *= 10;
			if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
				cents_frac += (s[i] - '0');
				i++;
			}
		}
		if (i < s.size() && s[i] >= '5' && s[i] <= '9') {
			cents_frac++;
		}
	}
	const std::int64_t total = whole * 100 + cents_frac;
	return negative ? -total : total;
}

/// Read an integer cents value, trying ``<key>_dollars`` (string
/// decimal dollars) first and falling back to ``<key>`` (raw cents)
/// if the ``_dollars`` field is absent. Kalshi's v2 REST schema
/// switched to the ``_dollars`` suffix while the kalshi-cpp struct
/// layout keeps cents — this helper lets both schemas deserialise
/// through the same code path.
std::int64_t extract_cents_or_dollars(const std::string& json, const std::string& key) {
	const std::string dollars_key = key + "_dollars";
	// Prefer the string-decimal-dollars shape if it's present.
	if (json.find("\"" + dollars_key + "\"") != std::string::npos) {
		return extract_dollar_cents(json, dollars_key);
	}
	return extract_int(json, key);
}

[[maybe_unused]] std::int64_t extract_fixed_point_int(const std::string& json,
													  const std::string& key) {
	const std::string s = extract_string(json, key);
	if (s.empty()) {
		return extract_int(json, key);
	}
	std::size_t i = 0;
	std::int64_t whole = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		whole = whole * 10 + (s[i] - '0');
		i++;
	}
	if (i < s.size() && s[i] == '.') {
		i++;
		if (i < s.size() && s[i] >= '5' && s[i] <= '9') {
			whole++;
		}
	}
	return whole;
}

bool extract_bool(const std::string& json, const std::string& key) {
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos)
		return false;

	pos = json.find(':', pos);
	if (pos == std::string::npos)
		return false;

	// Skip whitespace
	pos++;
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		pos++;

	return (pos < json.size() && json[pos] == 't');
}

// Parse ISO 8601 datetime string (e.g., "2023-11-07T05:31:56Z") to Unix timestamp
// Returns 0 if parsing fails
std::int64_t extract_datetime(const std::string& json, const std::string& key) {
	std::string datetime_str = extract_string(json, key);
	if (datetime_str.empty())
		return 0;

	// Expected format: YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS.sssZ
	// Minimum length: 20 (2023-11-07T05:31:56Z)
	if (datetime_str.size() < 20)
		return 0;

	// Parse individual components
	auto parse_int = [&datetime_str](size_t start, size_t len) -> int {
		int result = 0;
		for (size_t i = start; i < start + len && i < datetime_str.size(); ++i) {
			char c = datetime_str[i];
			if (c >= '0' && c <= '9') {
				result = result * 10 + (c - '0');
			}
		}
		return result;
	};

	int year = parse_int(0, 4);
	int month = parse_int(5, 2);
	int day = parse_int(8, 2);
	int hour = parse_int(11, 2);
	int min = parse_int(14, 2);
	int sec = parse_int(17, 2);

	// Days in each month (non-leap year)
	static const int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	// Check for leap year
	auto is_leap = [](int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };

	// Count days from Unix epoch (1970-01-01)
	int64_t days = 0;

	// Count days from years
	for (int y = 1970; y < year; ++y) {
		days += is_leap(y) ? 366 : 365;
	}

	// Count days from months
	for (int m = 1; m < month; ++m) {
		days += days_in_month[m];
		if (m == 2 && is_leap(year))
			days += 1;
	}

	// Add days (day 1 = 0 days offset)
	days += day - 1;

	return days * 86400 + static_cast<std::int64_t>(hour) * 3600 +
		   static_cast<std::int64_t>(min) * 60 + sec;
}

std::string extract_cursor(const std::string& json) {
	return extract_string(json, "cursor");
}

// Find the start of a JSON object by key
size_t find_object_start(const std::string& json, const std::string& key) {
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos)
		return std::string::npos;

	pos = json.find('{', pos);
	return pos;
}

// Find matching closing brace (tracks strings to avoid false matches)
size_t find_object_end(const std::string& json, size_t start) {
	if (start >= json.size() || json[start] != '{')
		return std::string::npos;

	int depth = 1;
	size_t pos = start + 1;
	bool in_string = false;
	while (pos < json.size() && depth > 0) {
		char c = json[pos];
		if (c == '"' && (pos == start + 1 || json[pos - 1] != '\\')) {
			// Check for even number of backslashes (proper escape handling)
			size_t backslash_count = 0;
			size_t check = pos;
			while (check > start && json[check - 1] == '\\') {
				backslash_count++;
				check--;
			}
			if (backslash_count % 2 == 0) {
				in_string = !in_string;
			}
		} else if (!in_string) {
			if (c == '{')
				depth++;
			else if (c == '}')
				depth--;
		}
		pos++;
	}
	return depth == 0 ? pos : std::string::npos;
}

// Find the start of a JSON array by key
size_t find_array_start(const std::string& json, const std::string& key) {
	std::string search = "\"" + key + "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos)
		return std::string::npos;

	pos = json.find('[', pos);
	return pos;
}

// Find matching closing bracket
size_t find_array_end(const std::string& json, size_t start) {
	if (start >= json.size() || json[start] != '[')
		return std::string::npos;

	int depth = 1;
	size_t pos = start + 1;
	bool in_string = false;
	while (pos < json.size() && depth > 0) {
		char c = json[pos];
		if (c == '"' && (pos == 0 || json[pos - 1] != '\\')) {
			in_string = !in_string;
		} else if (!in_string) {
			if (c == '[')
				depth++;
			else if (c == ']')
				depth--;
		}
		pos++;
	}
	return depth == 0 ? pos : std::string::npos;
}

// Extract array elements as separate JSON strings
std::vector<std::string> extract_array_objects(const std::string& json, const std::string& key) {
	std::vector<std::string> result;

	size_t array_start = find_array_start(json, key);
	if (array_start == std::string::npos)
		return result;

	size_t array_end = find_array_end(json, array_start);
	if (array_end == std::string::npos || array_end <= array_start + 1)
		return result;

	// Guard against buffer underflow: need at least 2 chars for content
	if (array_end - array_start < 2)
		return result;

	std::string array_content = json.substr(array_start + 1, array_end - array_start - 2);

	// Parse individual objects
	size_t pos = 0;
	while (pos < array_content.size()) {
		// Find next object start
		size_t obj_start = array_content.find('{', pos);
		if (obj_start == std::string::npos)
			break;

		size_t obj_end = find_object_end(array_content, obj_start);
		if (obj_end == std::string::npos)
			break;

		result.push_back(array_content.substr(obj_start, obj_end - obj_start));
		pos = obj_end;
	}

	return result;
}

std::vector<std::string> extract_string_array(const std::string& json, const std::string& key) {
	const std::size_t array_start = find_array_start(json, key);
	if (array_start == std::string::npos) {
		return {};
	}
	const std::size_t array_end = find_array_end(json, array_start);
	if (array_end == std::string::npos) {
		return {};
	}

	const std::string_view encoded{json.data() + array_start, array_end - array_start};
	const glz::expected<std::vector<std::string>, glz::error_ctx> parsed =
		glz::read_json<std::vector<std::string>>(encoded);
	return parsed ? *parsed : std::vector<std::string>{};
}

double extract_double(const std::string& json, const std::string& key) {
	const std::string search = "\"" + key + "\"";
	std::size_t pos = json.find(search);
	if (pos == std::string::npos) {
		return 0.0;
	}
	pos = json.find(':', pos + search.size());
	if (pos == std::string::npos) {
		return 0.0;
	}
	pos = json.find_first_not_of(" \t\r\n", pos + 1);
	if (pos == std::string::npos) {
		return 0.0;
	}
	const std::size_t end = json.find_first_of(",}", pos);
	const std::string_view encoded{json.data() + pos,
								   (end == std::string::npos ? json.size() : end) - pos};
	const glz::expected<double, glz::error_ctx> parsed = glz::read_json<double>(encoded);
	return parsed ? *parsed : 0.0;
}

std::string_view trim_json_scalar(std::string_view value) {
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
		value.remove_prefix(1);
	}
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
		value.remove_suffix(1);
	}
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
		value.remove_prefix(1);
		value.remove_suffix(1);
	}
	return value;
}

std::int32_t parse_int_literal(std::string_view raw) {
	const std::string_view value = trim_json_scalar(raw);
	if (value.empty())
		return 0;
	std::int32_t result = 0;
	auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
	if (ec != std::errc() || ptr != value.data() + value.size())
		return 0;
	return result;
}

[[maybe_unused]] std::int32_t parse_dollar_cents_literal(std::string_view raw) {
	const std::string_view value = trim_json_scalar(raw);
	std::size_t i = 0;
	bool negative = false;
	if (i < value.size() && value[i] == '-') {
		negative = true;
		i++;
	}

	std::int64_t whole = 0;
	while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
		if (whole > (std::numeric_limits<std::int64_t>::max() / 10)) {
			return negative ? std::numeric_limits<std::int32_t>::min()
							: std::numeric_limits<std::int32_t>::max();
		}
		whole = whole * 10 + (value[i] - '0');
		i++;
	}

	std::int64_t cents_frac = 0;
	if (i < value.size() && value[i] == '.') {
		i++;
		for (int d = 0; d < 2; d++) {
			cents_frac *= 10;
			if (i < value.size() && value[i] >= '0' && value[i] <= '9') {
				cents_frac += value[i] - '0';
				i++;
			}
		}
		if (i < value.size() && value[i] >= '5' && value[i] <= '9') {
			cents_frac++;
		}
	}

	const std::int64_t total = whole * 100 + cents_frac;
	const std::int64_t signed_total = negative ? -total : total;
	if (signed_total > std::numeric_limits<std::int32_t>::max())
		return std::numeric_limits<std::int32_t>::max();
	if (signed_total < std::numeric_limits<std::int32_t>::min())
		return std::numeric_limits<std::int32_t>::min();
	return static_cast<std::int32_t>(signed_total);
}

[[maybe_unused]] std::int32_t parse_fixed_point_int_literal(std::string_view raw) {
	const std::string_view value = trim_json_scalar(raw);
	std::size_t i = 0;
	bool negative = false;
	if (i < value.size() && value[i] == '-') {
		negative = true;
		i++;
	}

	std::int64_t whole = 0;
	while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
		if (whole > (std::numeric_limits<std::int64_t>::max() / 10)) {
			return negative ? std::numeric_limits<std::int32_t>::min()
							: std::numeric_limits<std::int32_t>::max();
		}
		whole = whole * 10 + (value[i] - '0');
		i++;
	}

	if (i < value.size() && value[i] == '.') {
		i++;
		if (i < value.size() && value[i] >= '5' && value[i] <= '9') {
			whole++;
		}
	}

	const std::int64_t signed_total = negative ? -whole : whole;
	if (signed_total > std::numeric_limits<std::int32_t>::max())
		return std::numeric_limits<std::int32_t>::max();
	if (signed_total < std::numeric_limits<std::int32_t>::min())
		return std::numeric_limits<std::int32_t>::min();
	return static_cast<std::int32_t>(signed_total);
}

std::int32_t exact_scaled_int_or_zero(std::string_view raw, std::uint8_t scale) {
	const std::string_view value = trim_json_scalar(raw);
	const Result<FixedPoint> parsed = FixedPoint::parse(value);
	if (!parsed) {
		return 0;
	}
	const Result<std::int64_t> scaled = parsed->scaled_integer(scale);
	if (!scaled || *scaled < std::numeric_limits<std::int32_t>::min() ||
		*scaled > std::numeric_limits<std::int32_t>::max()) {
		return 0;
	}
	return static_cast<std::int32_t>(*scaled);
}

std::string extract_quoted_scalar(const std::string& json, const std::string& key) {
	const std::size_t key_pos = json.find("\"" + key + "\"");
	if (key_pos == std::string::npos)
		return {};
	const std::size_t colon = json.find(':', key_pos + key.size() + 2);
	if (colon == std::string::npos)
		return {};
	const std::size_t quote = json.find_first_not_of(" \t\r\n", colon + 1);
	if (quote == std::string::npos || json[quote] != '"')
		return {};
	const std::size_t end = json.find('"', quote + 1);
	return end == std::string::npos ? std::string{} : json.substr(quote + 1, end - quote - 1);
}

Rfq parse_rfq_response(const std::string& json) {
	Rfq rfq;
	rfq.id = extract_string(json, "id");
	rfq.creator_id = extract_string(json, "creator_id");
	rfq.market_ticker = extract_string(json, "market_ticker");
	rfq.contracts_fp = extract_quoted_scalar(json, "contracts_fp");
	rfq.target_cost_dollars = extract_quoted_scalar(json, "target_cost_dollars");
	rfq.status = extract_string(json, "status");
	rfq.rest_remainder = extract_bool(json, "rest_remainder");
	rfq.cancellation_reason = extract_string(json, "cancellation_reason");
	rfq.creator_user_id = extract_string(json, "creator_user_id");
	rfq.creator_subaccount = extract_int(json, "creator_subaccount");
	rfq.created_ts = extract_string(json, "created_ts");
	rfq.cancelled_ts = extract_string(json, "cancelled_ts");
	rfq.updated_ts = extract_string(json, "updated_ts");
	rfq.count = exact_scaled_int_or_zero(rfq.contracts_fp, 0);
	return rfq;
}

Quote parse_quote_response(const std::string& json) {
	Quote quote;
	quote.id = extract_string(json, "id");
	quote.rfq_id = extract_string(json, "rfq_id");
	quote.creator_id = extract_string(json, "creator_id");
	quote.rfq_creator_id = extract_string(json, "rfq_creator_id");
	quote.market_ticker = extract_string(json, "market_ticker");
	quote.contracts_fp = extract_quoted_scalar(json, "contracts_fp");
	quote.yes_bid_dollars = extract_quoted_scalar(json, "yes_bid_dollars");
	quote.no_bid_dollars = extract_quoted_scalar(json, "no_bid_dollars");
	quote.created_ts = extract_string(json, "created_ts");
	quote.updated_ts = extract_string(json, "updated_ts");
	quote.status = extract_string(json, "status");
	quote.accepted_side = extract_string(json, "accepted_side");
	quote.accepted_ts = extract_string(json, "accepted_ts");
	quote.confirmed_ts = extract_string(json, "confirmed_ts");
	quote.executed_ts = extract_string(json, "executed_ts");
	quote.cancelled_ts = extract_string(json, "cancelled_ts");
	quote.rest_remainder = extract_bool(json, "rest_remainder");
	quote.post_only = extract_bool(json, "post_only");
	quote.cancellation_reason = extract_string(json, "cancellation_reason");
	quote.creator_subaccount = extract_int(json, "creator_subaccount");
	quote.rfq_creator_subaccount = extract_int(json, "rfq_creator_subaccount");
	quote.yes_contracts_fp = extract_quoted_scalar(json, "yes_contracts_fp");
	quote.no_contracts_fp = extract_quoted_scalar(json, "no_contracts_fp");
	quote.count = exact_scaled_int_or_zero(quote.contracts_fp, 0);
	quote.price = exact_scaled_int_or_zero(quote.yes_bid_dollars, 2);
	return quote;
}

std::vector<OrderBookEntry> parse_orderbook_entries(const std::string& json, const std::string& key,
													bool price_is_dollars,
													bool quantity_is_fixed_point) {
	std::vector<OrderBookEntry> entries;
	const size_t array_start = find_array_start(json, key);
	if (array_start == std::string::npos)
		return entries;

	const size_t array_end = find_array_end(json, array_start);
	if (array_end == std::string::npos || array_end <= array_start + 1)
		return entries;

	const std::string array = json.substr(array_start, array_end - array_start);
	size_t pos = 1;
	while (pos < array.size()) {
		const size_t inner_start = array.find('[', pos);
		if (inner_start == std::string::npos)
			break;

		const size_t comma = array.find(',', inner_start + 1);
		const size_t inner_end =
			comma == std::string::npos ? std::string::npos : array.find(']', comma + 1);
		if (comma == std::string::npos || inner_end == std::string::npos)
			break;

		const std::string_view price_raw{array.data() + inner_start + 1, comma - inner_start - 1};
		const std::string_view quantity_raw{array.data() + comma + 1, inner_end - comma - 1};

		OrderBookEntry entry;
		if (price_is_dollars) {
			entry.price_dollars = std::string(trim_json_scalar(price_raw));
			entry.price_cents = exact_scaled_int_or_zero(price_raw, 2);
		} else {
			entry.price_cents = parse_int_literal(price_raw);
		}
		if (quantity_is_fixed_point) {
			entry.quantity_fp = std::string(trim_json_scalar(quantity_raw));
			entry.quantity = exact_scaled_int_or_zero(quantity_raw, 0);
		} else {
			entry.quantity = parse_int_literal(quantity_raw);
		}
		entries.push_back(entry);

		pos = inner_end + 1;
	}

	return entries;
}

[[maybe_unused]] std::string escape_json_string(const std::string& s) {
	std::string result;
	result.reserve(s.size() + 10);
	for (char c : s) {
		switch (c) {
			case '"':
				result += "\\\"";
				break;
			case '\\':
				result += "\\\\";
				break;
			case '\n':
				result += "\\n";
				break;
			case '\r':
				result += "\\r";
				break;
			case '\t':
				result += "\\t";
				break;
			default:
				result += c;
				break;
		}
	}
	return result;
}

bool is_query_unreserved(unsigned char c) {
	return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

std::string percent_encode_query_value(std::string_view value) {
	constexpr char hex[] = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(value.size());
	for (unsigned char c : value) {
		if (is_query_unreserved(c)) {
			encoded.push_back(static_cast<char>(c));
		} else {
			encoded.push_back('%');
			encoded.push_back(hex[c >> 4]);
			encoded.push_back(hex[c & 0x0F]);
		}
	}
	return encoded;
}

void append_query_param(std::string& query, const std::string& key, const std::string& value) {
	if (value.empty())
		return;
	query += (query.find('?') == std::string::npos ? '?' : '&');
	query += key + "=" + percent_encode_query_value(value);
}

void append_query_param(std::string& query, const std::string& key, std::int32_t value) {
	query += (query.find('?') == std::string::npos ? '?' : '&');
	query += key + "=" + std::to_string(value);
}

void append_query_param(std::string& query, const std::string& key, std::int64_t value) {
	query += (query.find('?') == std::string::npos ? '?' : '&');
	query += key + "=" + std::to_string(value);
}

void append_query_param(std::string& query, const std::string& key, bool value) {
	append_query_param(query, key, value ? std::string{"true"} : std::string{"false"});
}

std::optional<std::int64_t> extract_optional_datetime(const std::string& json,
													  const std::string& key) {
	const std::int64_t value = extract_datetime(json, key);
	if (value <= 0) {
		return std::nullopt;
	}
	return value;
}

std::optional<std::int32_t> extract_optional_int32(const std::string& json,
												   const std::string& key) {
	if (json.find("\"" + key + "\"") == std::string::npos) {
		return std::nullopt;
	}
	return static_cast<std::int32_t>(extract_int(json, key));
}

std::optional<std::int64_t> extract_optional_int64(const std::string& json,
												   const std::string& key) {
	const std::string search = "\"" + key + "\"";
	const std::size_t key_position = json.find(search);
	if (key_position == std::string::npos) {
		return std::nullopt;
	}
	const std::size_t colon = json.find(':', key_position + search.size());
	if (colon == std::string::npos) {
		return std::nullopt;
	}
	const std::size_t value = json.find_first_not_of(" \t\r\n", colon + 1);
	if (value == std::string::npos || json.compare(value, 4, "null") == 0) {
		return std::nullopt;
	}
	return extract_int(json, key);
}

std::optional<std::int32_t> extract_optional_cents_or_dollars(const std::string& json,
															  const std::string& key) {
	const bool has_dollars = json.find("\"" + key + "_dollars\"") != std::string::npos;
	const bool has_cents = json.find("\"" + key + "\"") != std::string::npos;
	if (!has_dollars && !has_cents) {
		return std::nullopt;
	}
	return static_cast<std::int32_t>(extract_cents_or_dollars(json, key));
}

} // anonymous namespace

namespace api_detail {

Market parse_market_response(std::string_view body) {
	const std::string response_body{body};
	// Find market object (may be nested under "market" key or at root)
	size_t market_start = find_object_start(response_body, "market");
	std::string market_json =
		market_start != std::string::npos
			? response_body.substr(market_start,
								   find_object_end(response_body, market_start) - market_start)
			: response_body;

	Market market;
	market.ticker = extract_string(market_json, "ticker");
	market.event_ticker = extract_string(market_json, "event_ticker");
	market.market_type = extract_string(market_json, "market_type");
	market.title = extract_string(market_json, "title");
	market.subtitle = extract_string(market_json, "subtitle");
	market.yes_sub_title = extract_string(market_json, "yes_sub_title");
	market.no_sub_title = extract_string(market_json, "no_sub_title");
	market.yes_bid_dollars = extract_string(market_json, "yes_bid_dollars");
	market.yes_ask_dollars = extract_string(market_json, "yes_ask_dollars");
	market.no_bid_dollars = extract_string(market_json, "no_bid_dollars");
	market.no_ask_dollars = extract_string(market_json, "no_ask_dollars");
	market.last_price_dollars = extract_string(market_json, "last_price_dollars");
	market.volume_fp = extract_string(market_json, "volume_fp");
	market.volume_24h_fp = extract_string(market_json, "volume_24h_fp");
	market.open_interest_fp = extract_string(market_json, "open_interest_fp");
	market.notional_value_dollars = extract_string(market_json, "notional_value_dollars");
	market.previous_yes_bid_dollars = extract_string(market_json, "previous_yes_bid_dollars");
	market.previous_yes_ask_dollars = extract_string(market_json, "previous_yes_ask_dollars");
	market.previous_price_dollars = extract_string(market_json, "previous_price_dollars");
	market.settlement_value_dollars = extract_string(market_json, "settlement_value_dollars");
	market.price_level_structure = extract_string(market_json, "price_level_structure");
	market.rules_primary = extract_string(market_json, "rules_primary");
	market.rules_secondary = extract_string(market_json, "rules_secondary");
	market.exchange_index = static_cast<std::int32_t>(extract_int(market_json, "exchange_index"));

	std::string status_str = extract_string(market_json, "status");
	market.status = parse_market_status(status_str);

	// Time fields are ISO 8601 datetime strings in the Kalshi API response
	market.open_time = extract_datetime(market_json, "open_time");
	market.close_time = extract_datetime(market_json, "close_time");
	market.expected_expiration_time =
		extract_optional_datetime(market_json, "expected_expiration_time");
	market.expiration_time = extract_optional_datetime(market_json, "expiration_time");
	market.latest_expiration_time =
		extract_optional_datetime(market_json, "latest_expiration_time");
	market.settlement_ts = extract_optional_datetime(market_json, "settlement_ts");

	// Kalshi's v2 REST schema uses ``yes_bid_dollars`` / ``yes_ask_dollars``
	// etc. (string decimal dollars) in current responses but ``yes_bid`` /
	// ``yes_ask`` (raw cent integers) in archived responses. The
	// extract_cents_or_dollars helper accepts either shape, preferring
	// the ``_dollars`` form when present. Without this, open-market
	// rows land with 0s for every price field and the trader's
	// scanner reports "0 executable markets".
	market.yes_bid = static_cast<std::int32_t>(extract_cents_or_dollars(market_json, "yes_bid"));
	market.yes_ask = static_cast<std::int32_t>(extract_cents_or_dollars(market_json, "yes_ask"));
	market.no_bid = static_cast<std::int32_t>(extract_cents_or_dollars(market_json, "no_bid"));
	market.no_ask = static_cast<std::int32_t>(extract_cents_or_dollars(market_json, "no_ask"));
	market.volume = static_cast<std::int32_t>(extract_int(market_json, "volume"));
	market.open_interest = static_cast<std::int32_t>(extract_int(market_json, "open_interest"));
	market.settlement_timer_seconds =
		extract_optional_int32(market_json, "settlement_timer_seconds");
	market.settlement_value_cents =
		extract_optional_cents_or_dollars(market_json, "settlement_value");

	std::string expiration_value_str = extract_string(market_json, "expiration_value");
	if (!expiration_value_str.empty()) {
		market.expiration_value = expiration_value_str;
	}

	std::string result_str = extract_string(market_json, "result");
	if (!result_str.empty()) {
		market.result = result_str;
	}

	return market;
}

std::vector<Market> parse_markets_response(std::string_view body) {
	const std::string response_body{body};
	std::vector<Market> markets;
	std::vector<std::string> market_objects = extract_array_objects(response_body, "markets");
	markets.reserve(market_objects.size());

	for (const std::string& obj : market_objects) {
		markets.push_back(parse_market_response(obj));
	}

	return markets;
}

OrderBook parse_orderbook_response(std::string_view body) {
	const std::string response_body{body};
	OrderBook book;

	const size_t orderbook_start = find_object_start(response_body, "orderbook");
	const std::string orderbook_json =
		orderbook_start != std::string::npos
			? response_body.substr(orderbook_start,
								   find_object_end(response_body, orderbook_start) -
									   orderbook_start)
			: response_body;

	book.market_ticker = extract_string(orderbook_json, "market_ticker");
	if (book.market_ticker.empty()) {
		book.market_ticker = extract_string(response_body, "ticker");
	}

	const size_t fp_start = find_object_start(orderbook_json, "orderbook_fp");
	const std::string fp_json =
		fp_start != std::string::npos
			? orderbook_json.substr(fp_start, find_object_end(orderbook_json, fp_start) - fp_start)
			: orderbook_json;

	if (fp_json.find("\"yes_dollars\"") != std::string::npos ||
		fp_json.find("\"no_dollars\"") != std::string::npos) {
		book.yes_bids = parse_orderbook_entries(fp_json, "yes_dollars", true, true);
		book.no_bids = parse_orderbook_entries(fp_json, "no_dollars", true, true);
		return book;
	}

	book.yes_bids = parse_orderbook_entries(orderbook_json, "yes", false, false);
	book.no_bids = parse_orderbook_entries(orderbook_json, "no", false, false);
	return book;
}

std::vector<OrderBook> parse_orderbooks_response(std::string_view body) {
	const std::string response_body{body};
	std::vector<OrderBook> books;
	const std::vector<std::string> objs = extract_array_objects(response_body, "orderbooks");
	books.reserve(objs.size());
	for (const std::string& obj : objs) {
		books.push_back(parse_orderbook_response(obj));
	}
	return books;
}

std::vector<Candlestick> parse_candlesticks_response(std::string_view body) {
	const std::string response_body{body};
	std::vector<Candlestick> candlesticks;
	std::vector<std::string> candle_objects =
		extract_array_objects(response_body, "market_candlesticks");

	if (candle_objects.empty() && !response_body.empty()) {
		if (response_body.find("candlesticks") != std::string::npos &&
			response_body.find("market_candlesticks") == std::string::npos) {
			candle_objects = extract_array_objects(response_body, "candlesticks");
		}
	}

	candlesticks.reserve(candle_objects.size());
	for (const std::string& obj : candle_objects) {
		Candlestick c;
		c.timestamp = extract_int(obj, "end_period_ts");
		c.volume_fp = extract_quoted_scalar(obj, "volume_fp");
		if (c.volume_fp.empty())
			c.volume_fp = extract_quoted_scalar(obj, "volume");
		c.volume = c.volume_fp.empty() ? static_cast<std::int32_t>(extract_int(obj, "volume"))
									   : exact_scaled_int_or_zero(c.volume_fp, 0);

		const size_t price_pos = obj.find("\"price\"");
		if (price_pos != std::string::npos) {
			const size_t brace_start = obj.find('{', price_pos);
			if (brace_start != std::string::npos) {
				int depth = 1;
				size_t brace_end = brace_start + 1;
				while (brace_end < obj.size() && depth > 0) {
					if (obj[brace_end] == '{')
						depth++;
					else if (obj[brace_end] == '}')
						depth--;
					brace_end++;
				}
				const std::string price_obj = obj.substr(brace_start, brace_end - brace_start);
				c.open_price_dollars = extract_quoted_scalar(price_obj, "open_dollars");
				c.close_price_dollars = extract_quoted_scalar(price_obj, "close_dollars");
				c.high_price_dollars = extract_quoted_scalar(price_obj, "high_dollars");
				c.low_price_dollars = extract_quoted_scalar(price_obj, "low_dollars");
				if (c.open_price_dollars.empty())
					c.open_price_dollars = extract_quoted_scalar(price_obj, "open");
				if (c.close_price_dollars.empty())
					c.close_price_dollars = extract_quoted_scalar(price_obj, "close");
				if (c.high_price_dollars.empty())
					c.high_price_dollars = extract_quoted_scalar(price_obj, "high");
				if (c.low_price_dollars.empty())
					c.low_price_dollars = extract_quoted_scalar(price_obj, "low");
				c.open_price = c.open_price_dollars.empty()
								   ? static_cast<std::int32_t>(extract_int(price_obj, "open"))
								   : exact_scaled_int_or_zero(c.open_price_dollars, 2);
				c.close_price = c.close_price_dollars.empty()
									? static_cast<std::int32_t>(extract_int(price_obj, "close"))
									: exact_scaled_int_or_zero(c.close_price_dollars, 2);
				c.high_price = c.high_price_dollars.empty()
								   ? static_cast<std::int32_t>(extract_int(price_obj, "high"))
								   : exact_scaled_int_or_zero(c.high_price_dollars, 2);
				c.low_price = c.low_price_dollars.empty()
								  ? static_cast<std::int32_t>(extract_int(price_obj, "low"))
								  : exact_scaled_int_or_zero(c.low_price_dollars, 2);
			}
		}
		candlesticks.push_back(c);
	}

	return candlesticks;
}

namespace {

/// Returns nullopt if ``"<key>"`` appears followed by the literal token
/// ``null`` (Kalshi's representation for unset timestamps); otherwise
/// returns the parsed int (or 0 if the key is missing entirely — same
/// behaviour as ``extract_int``).
std::optional<std::int64_t> extract_nullable_int(const std::string& json, const std::string& key) {
	const std::string search = "\"" + key + "\"";
	const std::size_t kpos = json.find(search);
	if (kpos == std::string::npos)
		return std::nullopt;

	std::size_t pos = json.find(':', kpos);
	if (pos == std::string::npos)
		return std::nullopt;
	pos++;
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		pos++;
	if (pos + 4 <= json.size() && json.compare(pos, 4, "null") == 0) {
		return std::nullopt;
	}
	return extract_int(json, key);
}

template <typename T>
std::vector<T> parse_portfolio_movements(std::string_view body, const std::string& array_key) {
	const std::string response_body{body};
	std::vector<T> out;
	const std::vector<std::string> objs = extract_array_objects(response_body, array_key);
	out.reserve(objs.size());
	for (const std::string& obj : objs) {
		T row;
		row.id = extract_string(obj, "id");
		row.status = extract_string(obj, "status");
		row.type = extract_string(obj, "type");
		row.amount_cents = extract_int(obj, "amount_cents");
		row.fee_cents = extract_int(obj, "fee_cents");
		row.created_ts = extract_int(obj, "created_ts");
		row.finalized_ts = extract_nullable_int(obj, "finalized_ts");
		out.push_back(std::move(row));
	}
	return out;
}

} // anonymous namespace

std::vector<Deposit> parse_deposits_response(std::string_view body) {
	return parse_portfolio_movements<Deposit>(body, "deposits");
}

std::vector<Withdrawal> parse_withdrawals_response(std::string_view body) {
	return parse_portfolio_movements<Withdrawal>(body, "withdrawals");
}

std::vector<PublicTrade> parse_trades_response(std::string_view body) {
	const std::string buf{body};
	const std::vector<std::string> trade_objects = extract_array_objects(buf, "trades");
	std::vector<PublicTrade> trades;
	trades.reserve(trade_objects.size());
	for (const std::string& obj : trade_objects) {
		PublicTrade t;
		t.trade_id = extract_string(obj, "trade_id");
		t.market_ticker = extract_string(obj, "ticker");
		t.yes_price_dollars = extract_string(obj, "yes_price_dollars");
		t.no_price_dollars = extract_string(obj, "no_price_dollars");
		t.count_fp = extract_string(obj, "count_fp");
		t.yes_price = t.yes_price_dollars.empty()
						  ? static_cast<std::int32_t>(extract_int(obj, "yes_price"))
						  : exact_scaled_int_or_zero(t.yes_price_dollars, 2);
		t.no_price = t.no_price_dollars.empty()
						 ? static_cast<std::int32_t>(extract_int(obj, "no_price"))
						 : exact_scaled_int_or_zero(t.no_price_dollars, 2);
		t.count = t.count_fp.empty() ? static_cast<std::int32_t>(extract_int(obj, "count"))
									 : exact_scaled_int_or_zero(t.count_fp, 0);
		t.taker_side = parse_side(extract_string(obj, "taker_side"));
		t.taker_outcome_side =
			extract_string(obj, "taker_outcome_side") == "no" ? OutcomeSide::No : OutcomeSide::Yes;
		t.taker_book_side =
			extract_string(obj, "taker_book_side") == "ask" ? BookSide::Ask : BookSide::Bid;
		t.created_time_iso = extract_string(obj, "created_time");
		if (t.created_time_iso.empty())
			t.created_time = extract_int(obj, "created_time");
		t.is_block_trade = extract_bool(obj, "is_block_trade");
		trades.push_back(t);
	}
	return trades;
}

OrderCancelResult parse_order_cancel_result_response(std::string_view body) {
	const std::string obj{body};
	OrderCancelResult result;
	result.order_id = extract_string(obj, "order_id");
	result.reduced_by = extract_string(obj, "reduced_by");
	result.ts_ms = extract_int(obj, "ts_ms");
	result.client_order_id = extract_string(obj, "client_order_id");

	const size_t error_start = find_object_start(obj, "error");
	if (error_start != std::string::npos) {
		const size_t error_end = find_object_end(obj, error_start);
		if (error_end != std::string::npos && error_end > error_start) {
			const std::string error_obj = obj.substr(error_start, error_end - error_start);
			OrderCancelError error;
			error.code = extract_string(error_obj, "code");
			error.message = extract_string(error_obj, "message");
			error.details = extract_string(error_obj, "details");
			error.service = extract_string(error_obj, "service");
			if (!error.code.empty() || !error.message.empty() || !error.details.empty() ||
				!error.service.empty()) {
				result.error = std::move(error);
			}
		}
	}

	return result;
}

std::vector<OrderCancelResult> parse_batch_order_cancel_result_response(std::string_view body) {
	const std::string response_body{body};
	std::vector<OrderCancelResult> results;
	const std::vector<std::string> objs = extract_array_objects(response_body, "orders");
	results.reserve(objs.size());
	for (const std::string& obj : objs) {
		results.push_back(parse_order_cancel_result_response(obj));
	}
	return results;
}

AccountApiLimits parse_account_api_limits_response(std::string_view body) {
	const std::string response_body{body};
	AccountApiLimits result;
	result.usage_tier = extract_string(response_body, "usage_tier");

	const size_t read_start = find_object_start(response_body, "read");
	if (read_start != std::string::npos) {
		const size_t read_end = find_object_end(response_body, read_start);
		if (read_end != std::string::npos && read_end > read_start) {
			const std::string read_json = response_body.substr(read_start, read_end - read_start);
			result.read.refill_rate = extract_int(read_json, "refill_rate");
			result.read.bucket_capacity = extract_int(read_json, "bucket_capacity");
		}
	}

	const size_t write_start = find_object_start(response_body, "write");
	if (write_start != std::string::npos) {
		const size_t write_end = find_object_end(response_body, write_start);
		if (write_end != std::string::npos && write_end > write_start) {
			const std::string write_json =
				response_body.substr(write_start, write_end - write_start);
			result.write.refill_rate = extract_int(write_json, "refill_rate");
			result.write.bucket_capacity = extract_int(write_json, "bucket_capacity");
		}
	}

	return result;
}

EndpointCosts parse_endpoint_costs_response(std::string_view body) {
	const std::string response_body{body};
	EndpointCosts result;
	result.default_cost = extract_int(response_body, "default_cost");

	const std::vector<std::string> objs = extract_array_objects(response_body, "endpoint_costs");
	result.endpoint_costs.reserve(objs.size());
	for (const std::string& obj : objs) {
		EndpointCost row;
		row.method = extract_string(obj, "method");
		row.path = extract_string(obj, "path");
		row.cost = extract_int(obj, "cost");
		result.endpoint_costs.push_back(std::move(row));
	}
	return result;
}

std::string build_series_query_string(const GetSeriesParams& params) {
	std::string query = "/series";
	if (params.category)
		append_query_param(query, "category", *params.category);
	if (params.tags)
		append_query_param(query, "tags", *params.tags);
	if (params.include_product_metadata)
		append_query_param(query, "include_product_metadata", *params.include_product_metadata);
	if (params.include_volume)
		append_query_param(query, "include_volume", *params.include_volume);
	if (params.min_updated_ts)
		append_query_param(query, "min_updated_ts", *params.min_updated_ts);
	return query;
}

std::string build_cancel_order_v2_path(const CancelOrderV2Params& params) {
	std::string path = "/portfolio/events/orders/" + params.order_id;
	if (params.subaccount)
		append_query_param(path, "subaccount", *params.subaccount);
	if (params.exchange_index)
		append_query_param(path, "exchange_index", *params.exchange_index);
	if (params.market_ticker)
		append_query_param(path, "market_ticker", *params.market_ticker);
	return path;
}

} // namespace api_detail

// ===== Exchange API =====

Result<ExchangeStatus> KalshiClient::get_exchange_status() {
	Result<HttpResponse> response = impl_->transport->get("/exchange/status");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get exchange status: " + std::to_string(response->status_code),
				  response->status_code});
	}

	ExchangeStatus status;
	status.trading_active = extract_bool(response->body, "trading_active");
	status.exchange_active = extract_bool(response->body, "exchange_active");
	return status;
}

Result<AccountApiLimits> KalshiClient::get_account_api_limits() {
	Result<HttpResponse> response = impl_->transport->get("/account/limits");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get account API limits: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return api_detail::parse_account_api_limits_response(response->body);
}

Result<EndpointCosts> KalshiClient::get_endpoint_costs() {
	Result<HttpResponse> response = impl_->transport->get("/account/endpoint_costs");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get endpoint costs: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return api_detail::parse_endpoint_costs_response(response->body);
}

// ===== Markets API =====

Result<Market> KalshiClient::get_market(const std::string& ticker) {
	Result<HttpResponse> response = impl_->transport->get("/markets/" + ticker);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get market: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return parse_market(response->body);
}

Result<Market> KalshiClient::parse_market(const std::string& json) {
	return api_detail::parse_market_response(json);
}

Result<std::vector<Market>> KalshiClient::parse_markets(const std::string& json) {
	return api_detail::parse_markets_response(json);
}

std::string KalshiClient::build_markets_query(const GetMarketsParams& params) {
	std::string query = "/markets";

	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.event_ticker)
		append_query_param(query, "event_ticker", *params.event_ticker);
	if (params.series_ticker)
		append_query_param(query, "series_ticker", *params.series_ticker);
	if (params.status)
		append_query_param(query, "status", *params.status);
	if (params.tickers)
		append_query_param(query, "tickers", *params.tickers);
	if (params.min_created_ts)
		append_query_param(query, "min_created_ts", *params.min_created_ts);
	if (params.max_created_ts)
		append_query_param(query, "max_created_ts", *params.max_created_ts);
	if (params.min_updated_ts)
		append_query_param(query, "min_updated_ts", *params.min_updated_ts);
	if (params.max_close_ts)
		append_query_param(query, "max_close_ts", *params.max_close_ts);
	if (params.min_close_ts)
		append_query_param(query, "min_close_ts", *params.min_close_ts);
	if (params.min_settled_ts)
		append_query_param(query, "min_settled_ts", *params.min_settled_ts);
	if (params.max_settled_ts)
		append_query_param(query, "max_settled_ts", *params.max_settled_ts);
	if (params.mve_filter)
		append_query_param(query, "mve_filter", *params.mve_filter);

	return query;
}

Result<PaginatedResponse<Market>> KalshiClient::get_markets(const GetMarketsParams& params) {
	std::string path = build_markets_query(params);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get markets: " + std::to_string(response->status_code),
				  response->status_code});
	}

	Result<std::vector<Market>> markets = parse_markets(response->body);
	if (!markets) {
		return std::unexpected(markets.error());
	}

	PaginatedResponse<Market> result;
	result.items = std::move(*markets);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

Result<OrderBook> KalshiClient::get_market_orderbook(const std::string& ticker,
													 std::optional<std::int32_t> depth) {
	std::string path = "/markets/" + ticker + "/orderbook";
	if (depth) {
		append_query_param(path, "depth", *depth);
	}

	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get orderbook: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return parse_orderbook(response->body);
}

Result<OrderBook> KalshiClient::parse_orderbook(const std::string& json) {
	return api_detail::parse_orderbook_response(json);
}

Result<std::vector<OrderBook>>
KalshiClient::get_market_orderbooks(const std::vector<std::string>& tickers) {
	if (tickers.empty()) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "get_market_orderbooks requires at least one ticker"});
	}
	if (tickers.size() > 100) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "get_market_orderbooks accepts at most 100 tickers"});
	}

	std::string path = "/markets/orderbooks";
	std::string joined;
	for (const std::string& ticker : tickers) {
		if (!joined.empty()) {
			joined.push_back(',');
		}
		joined += ticker;
	}
	append_query_param(path, "tickers", joined);

	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get orderbooks: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return parse_orderbooks(response->body);
}

Result<std::vector<OrderBook>> KalshiClient::parse_orderbooks(const std::string& json) {
	return api_detail::parse_orderbooks_response(json);
}

Result<std::vector<Candlestick>>
KalshiClient::get_market_candlesticks(const GetCandlesticksParams& params) {
	const std::string& series_ticker =
		params.series_ticker.empty() ? params.event_ticker : params.series_ticker;
	if (series_ticker.empty() || params.ticker.empty() || !params.start_ts || !params.end_ts) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest,
				  "candlesticks require series_ticker, ticker, start_ts, and end_ts"});
	}
	std::string path = "/series/" + series_ticker + "/markets/" + params.ticker + "/candlesticks";
	append_query_param(path, "period_interval", params.period_interval);
	if (params.start_ts)
		append_query_param(path, "start_ts", *params.start_ts);
	if (params.end_ts)
		append_query_param(path, "end_ts", *params.end_ts);
	if (params.include_latest_before_start)
		append_query_param(path, "include_latest_before_start",
						   *params.include_latest_before_start);

	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to get candlesticks: HTTP " +
										 std::to_string(response->status_code) + " - " +
										 response->body.substr(0, 200),
									 response->status_code});
	}

	return api_detail::parse_candlesticks_response(response->body);
}

std::string KalshiClient::build_trades_query(const GetTradesParams& params) {
	std::string query = "/markets/trades";

	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.market_ticker)
		append_query_param(query, "ticker", *params.market_ticker);
	if (params.min_ts)
		append_query_param(query, "min_ts", *params.min_ts);
	if (params.max_ts)
		append_query_param(query, "max_ts", *params.max_ts);

	return query;
}

Result<PaginatedResponse<PublicTrade>> KalshiClient::get_trades(const GetTradesParams& params) {
	std::string path = build_trades_query(params);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get trades: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<PublicTrade> result;
	result.items = api_detail::parse_trades_response(response->body);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

// ===== Events API =====

std::string KalshiClient::build_events_query(const GetEventsParams& params) {
	std::string query = "/events";

	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.series_ticker)
		append_query_param(query, "series_ticker", *params.series_ticker);
	if (params.status)
		append_query_param(query, "status", *params.status);
	if (params.with_nested_markets)
		append_query_param(query, "with_nested_markets", *params.with_nested_markets);
	if (params.with_milestones)
		append_query_param(query, "with_milestones", *params.with_milestones);
	if (params.event_tickers)
		append_query_param(query, "event_tickers", *params.event_tickers);
	if (params.min_close_ts)
		append_query_param(query, "min_close_ts", *params.min_close_ts);
	if (params.min_updated_ts)
		append_query_param(query, "min_updated_ts", *params.min_updated_ts);

	return query;
}

Result<Event> KalshiClient::get_event(const std::string& event_ticker) {
	Result<HttpResponse> response = impl_->transport->get("/events/" + event_ticker);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{
			ErrorCode::ServerError, "Failed to get event: " + std::to_string(response->status_code),
			response->status_code});
	}

	// Find event object
	size_t evt_start = find_object_start(response->body, "event");
	std::string evt_json =
		evt_start != std::string::npos
			? response->body.substr(evt_start,
									find_object_end(response->body, evt_start) - evt_start)
			: response->body;

	Event event;
	event.event_ticker = extract_string(evt_json, "event_ticker");
	event.series_ticker = extract_string(evt_json, "series_ticker");
	event.title = extract_string(evt_json, "title");
	event.category = extract_string(evt_json, "category");
	event.sub_title = extract_string(evt_json, "sub_title");
	event.collateral_return_type = extract_string(evt_json, "collateral_return_type");
	event.mutually_exclusive = extract_bool(evt_json, "mutually_exclusive");
	event.available_on_brokers = extract_bool(evt_json, "available_on_brokers");
	event.last_updated_ts = extract_string(evt_json, "last_updated_ts");
	event.fee_type_override = extract_string(evt_json, "fee_type_override");
	event.exchange_index = static_cast<std::int32_t>(extract_int(evt_json, "exchange_index"));
	for (const std::string& obj : extract_array_objects(evt_json, "settlement_sources")) {
		event.settlement_source_details.push_back(SettlementSource{
			.name = extract_string(obj, "name"), .url = extract_string(obj, "url")});
	}

	return event;
}

Result<PaginatedResponse<Event>> KalshiClient::get_events(const GetEventsParams& params) {
	std::string path = build_events_query(params);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get events: " + std::to_string(response->status_code),
				  response->status_code});
	}

	std::vector<Event> events;
	std::vector<std::string> event_objects = extract_array_objects(response->body, "events");

	for (const std::string& obj : event_objects) {
		Event e;
		e.event_ticker = extract_string(obj, "event_ticker");
		e.series_ticker = extract_string(obj, "series_ticker");
		e.title = extract_string(obj, "title");
		e.category = extract_string(obj, "category");
		e.sub_title = extract_string(obj, "sub_title");
		e.collateral_return_type = extract_string(obj, "collateral_return_type");
		e.mutually_exclusive = extract_bool(obj, "mutually_exclusive");
		e.available_on_brokers = extract_bool(obj, "available_on_brokers");
		e.last_updated_ts = extract_string(obj, "last_updated_ts");
		e.fee_type_override = extract_string(obj, "fee_type_override");
		e.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		for (const std::string& source : extract_array_objects(obj, "settlement_sources")) {
			e.settlement_source_details.push_back(SettlementSource{
				.name = extract_string(source, "name"), .url = extract_string(source, "url")});
		}
		e.markets = api_detail::parse_markets_response(obj);
		events.push_back(e);
	}

	PaginatedResponse<Event> result;
	result.items = std::move(events);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

// ===== Series API =====

Result<Series> KalshiClient::get_series(const std::string& series_ticker) {
	Result<HttpResponse> response = impl_->transport->get("/series/" + series_ticker);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get series: " + std::to_string(response->status_code),
				  response->status_code});
	}

	size_t series_start = find_object_start(response->body, "series");
	std::string series_json =
		series_start != std::string::npos
			? response->body.substr(series_start,
									find_object_end(response->body, series_start) - series_start)
			: response->body;

	Series series;
	series.ticker = extract_string(series_json, "ticker");
	series.title = extract_string(series_json, "title");
	series.category = extract_string(series_json, "category");
	series.frequency = extract_string(series_json, "frequency");
	series.tags = extract_string_array(series_json, "tags");
	series.contract_url = extract_string(series_json, "contract_url");
	series.contract_terms_url = extract_string(series_json, "contract_terms_url");
	series.fee_type = extract_string(series_json, "fee_type");
	series.fee_multiplier = extract_double(series_json, "fee_multiplier");
	series.additional_prohibitions = extract_string_array(series_json, "additional_prohibitions");
	series.volume_fp = extract_string(series_json, "volume_fp");
	series.last_updated_ts = extract_string(series_json, "last_updated_ts");
	series.exchange_index = static_cast<std::int32_t>(extract_int(series_json, "exchange_index"));
	for (const std::string& obj : extract_array_objects(series_json, "settlement_sources")) {
		series.settlement_source_details.push_back(SettlementSource{
			.name = extract_string(obj, "name"), .url = extract_string(obj, "url")});
	}

	return series;
}

// ===== Portfolio API =====

Result<Balance> KalshiClient::get_balance(const GetBalanceParams& params) {
	std::string path = "/portfolio/balance";
	if (params.subaccount) {
		append_query_param(path, "subaccount", *params.subaccount);
	}
	if (params.exchange_index) {
		append_query_param(path, "exchange_index", *params.exchange_index);
	}
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get balance: " + std::to_string(response->status_code),
				  response->status_code});
	}

	Balance balance;
	// Keep the integer fields on their documented integer wire values. Exact
	// dollar strings are exposed separately and are never rounded into cents.
	balance.balance = extract_int(response->body, "balance");
	balance.available_balance = extract_int(response->body, "available_balance");
	balance.balance_dollars = extract_string(response->body, "balance_dollars");
	balance.portfolio_value = extract_int(response->body, "portfolio_value");
	balance.updated_ts = extract_int(response->body, "updated_ts");
	for (const std::string& obj : extract_array_objects(response->body, "balance_breakdown")) {
		balance.balance_breakdown.push_back(IndexedBalance{
			.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index")),
			.balance_dollars = extract_string(obj, "balance")});
	}

	return balance;
}

std::string KalshiClient::build_positions_query(const GetPositionsParams& params) {
	std::string query = "/portfolio/positions";

	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.event_ticker)
		append_query_param(query, "event_ticker", *params.event_ticker);
	if (params.market_ticker)
		append_query_param(query, "ticker", *params.market_ticker);
	if (params.count_filter)
		append_query_param(query, "count_filter", *params.count_filter);
	if (params.subaccount)
		append_query_param(query, "subaccount", *params.subaccount);
	if (params.exchange_index)
		append_query_param(query, "exchange_index", *params.exchange_index);

	return query;
}

Result<PaginatedResponse<Position>> KalshiClient::get_positions(const GetPositionsParams& params) {
	std::string path = build_positions_query(params);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get positions: " + std::to_string(response->status_code),
				  response->status_code});
	}

	std::vector<Position> positions;
	std::vector<std::string> pos_objects =
		extract_array_objects(response->body, "market_positions");

	for (const std::string& obj : pos_objects) {
		Position p;
		p.market_ticker = extract_string(obj, "ticker");
		p.position_fp = extract_string(obj, "position_fp");
		p.total_traded_dollars = extract_string(obj, "total_traded_dollars");
		p.market_exposure_dollars = extract_string(obj, "market_exposure_dollars");
		p.realized_pnl_dollars = extract_string(obj, "realized_pnl_dollars");
		p.fees_paid_dollars = extract_string(obj, "fees_paid_dollars");
		p.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		p.yes_contracts = exact_scaled_int_or_zero(p.position_fp, 0);
		p.total_cost_cents = exact_scaled_int_or_zero(p.total_traded_dollars, 2);
		positions.push_back(p);
	}

	PaginatedResponse<Position> result;
	result.items = std::move(positions);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

std::string KalshiClient::build_orders_query(const GetOrdersParams& params) {
	std::string query = "/portfolio/orders";

	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.market_ticker)
		append_query_param(query, "ticker", *params.market_ticker);
	if (params.event_ticker)
		append_query_param(query, "event_ticker", *params.event_ticker);
	if (params.min_ts)
		append_query_param(query, "min_ts", *params.min_ts);
	if (params.max_ts)
		append_query_param(query, "max_ts", *params.max_ts);
	if (params.status)
		append_query_param(query, "status", *params.status);
	if (params.subaccount)
		append_query_param(query, "subaccount", *params.subaccount);
	if (params.exchange_index)
		append_query_param(query, "exchange_index", *params.exchange_index);

	return query;
}

Result<Order> KalshiClient::parse_order(const std::string& json) {
	// Find order object
	size_t order_start = find_object_start(json, "order");
	std::string order_json =
		order_start != std::string::npos
			? json.substr(order_start, find_object_end(json, order_start) - order_start)
			: json;

	Order order;
	order.order_id = extract_string(order_json, "order_id");
	order.user_id = extract_string(order_json, "user_id");
	order.market_ticker = extract_string(order_json, "ticker");
	order.client_order_id = extract_string(order_json, "client_order_id");
	order.side = parse_side(extract_string(order_json, "side"));
	order.action = parse_action(extract_string(order_json, "action"));
	const std::string outcome_side = extract_string(order_json, "outcome_side");
	order.outcome_side = outcome_side.empty()
						 ? derive_outcome_side(order.side, order.action)
						 : (outcome_side == "no" ? OutcomeSide::No : OutcomeSide::Yes);
	const std::string book_side = extract_string(order_json, "book_side");
	order.book_side = book_side.empty() ? derive_book_side(order.side, order.action)
										 : (book_side == "ask" ? BookSide::Ask : BookSide::Bid);
	order.exchange_index = static_cast<std::int32_t>(extract_int(order_json, "exchange_index"));

	std::string type_str = extract_string(order_json, "type");
	order.type = (type_str == "market") ? OrderType::Market : OrderType::Limit;

	order.status = parse_order_status(extract_string(order_json, "status"));

	order.initial_count_fp = extract_string(order_json, "initial_count_fp");
	order.remaining_count_fp = extract_string(order_json, "remaining_count_fp");
	order.fill_count_fp = extract_string(order_json, "fill_count_fp");
	if (order.fill_count_fp.empty()) {
		order.fill_count_fp = extract_string(order_json, "fill_count");
	}
	if (order.remaining_count_fp.empty()) {
		order.remaining_count_fp = extract_string(order_json, "remaining_count");
	}
	order.initial_count = exact_scaled_int_or_zero(order.initial_count_fp, 0);
	if (order.initial_count_fp.empty()) {
		order.initial_count = static_cast<std::int32_t>(extract_int(order_json, "original_count"));
	}
	if (order.initial_count == 0) {
		const std::int64_t legacy_count = extract_int(order_json, "count");
		if (legacy_count != 0) {
			order.initial_count = static_cast<std::int32_t>(legacy_count);
		}
	}
	order.remaining_count = exact_scaled_int_or_zero(order.remaining_count_fp, 0);
	if (order.remaining_count_fp.empty()) {
		order.remaining_count =
			static_cast<std::int32_t>(extract_int(order_json, "remaining_count"));
	}
	order.filled_count = exact_scaled_int_or_zero(order.fill_count_fp, 0);
	if (order.fill_count_fp.empty()) {
		order.filled_count = order.initial_count - order.remaining_count;
	}

	// Price might be yes_price or no_price depending on side
	order.yes_price_dollars = extract_string(order_json, "yes_price_dollars");
	order.no_price_dollars = extract_string(order_json, "no_price_dollars");
	order.taker_fill_cost_dollars = extract_string(order_json, "taker_fill_cost_dollars");
	order.maker_fill_cost_dollars = extract_string(order_json, "maker_fill_cost_dollars");
	order.taker_fees_dollars = extract_string(order_json, "taker_fees_dollars");
	order.maker_fees_dollars = extract_string(order_json, "maker_fees_dollars");
	order.price = exact_scaled_int_or_zero(order.yes_price_dollars, 2);
	if (order.yes_price_dollars.empty()) {
		order.price = static_cast<std::int32_t>(extract_int(order_json, "yes_price"));
	}
	if (order.price == 0 && order.yes_price_dollars.empty()) {
		order.price = static_cast<std::int32_t>(extract_int(order_json, "no_price"));
	}

	order.created_time_iso = extract_string(order_json, "created_time");
	order.created_time = order.created_time_iso.empty() ? extract_int(order_json, "created_time")
													 : extract_datetime(order_json, "created_time");

	order.expiration_time = extract_string(order_json, "expiration_time");
	std::int64_t exp = order.expiration_time.empty()
						 ? extract_int(order_json, "expiration_ts")
						 : extract_datetime(order_json, "expiration_time");
	if (exp > 0) {
		order.expiration_ts = exp;
	}
	order.last_update_time = extract_string(order_json, "last_update_time");
	order.self_trade_prevention_type =
		extract_string(order_json, "self_trade_prevention_type");
	order.order_group_id = extract_string(order_json, "order_group_id");

	// V2 order-mutating endpoints (create / amend / decrease / batch_*)
	// carry a top-level `ts_ms` matching-engine timestamp alongside the
	// wrapped `order` object (added 2026-05-05). When parse_order is
	// called from a list endpoint each `obj` is just the order body
	// (no ts_ms sibling) and extract_int returns 0 → leave nullopt.
	std::int64_t ts_ms = extract_int(json, "ts_ms");
	if (ts_ms > 0) {
		order.mutation_ts_ms = ts_ms;
	}

	return order;
}

Result<std::vector<Order>> KalshiClient::parse_orders(const std::string& json) {
	std::vector<Order> orders;
	std::vector<std::string> order_objects = extract_array_objects(json, "orders");

	for (const std::string& obj : order_objects) {
		Result<Order> order = parse_order(obj);
		if (order) {
			orders.push_back(std::move(*order));
		}
	}

	return orders;
}

Result<PaginatedResponse<Order>> KalshiClient::get_orders(const GetOrdersParams& params) {
	std::string path = build_orders_query(params);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get orders: " + std::to_string(response->status_code),
				  response->status_code});
	}

	Result<std::vector<Order>> orders = parse_orders(response->body);
	if (!orders) {
		return std::unexpected(orders.error());
	}

	PaginatedResponse<Order> result;
	result.items = std::move(*orders);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

Result<Order> KalshiClient::get_order(const std::string& order_id) {
	Result<HttpResponse> response = impl_->transport->get("/portfolio/orders/" + order_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{
			ErrorCode::ServerError, "Failed to get order: " + std::to_string(response->status_code),
			response->status_code});
	}

	return parse_order(response->body);
}

std::string KalshiClient::build_fills_query(const GetFillsParams& params) {
	std::string query = "/portfolio/fills";

	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.market_ticker)
		append_query_param(query, "ticker", *params.market_ticker);
	if (params.order_id)
		append_query_param(query, "order_id", *params.order_id);
	if (params.min_ts)
		append_query_param(query, "min_ts", *params.min_ts);
	if (params.max_ts)
		append_query_param(query, "max_ts", *params.max_ts);
	if (params.subaccount)
		append_query_param(query, "subaccount", *params.subaccount);
	if (params.exchange_index)
		append_query_param(query, "exchange_index", *params.exchange_index);

	return query;
}

Result<PaginatedResponse<Fill>> KalshiClient::get_fills(const GetFillsParams& params) {
	std::string path = build_fills_query(params);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{
			ErrorCode::ServerError, "Failed to get fills: " + std::to_string(response->status_code),
			response->status_code});
	}

	std::vector<Fill> fills;
	std::vector<std::string> fill_objects = extract_array_objects(response->body, "fills");

	for (const std::string& obj : fill_objects) {
		Fill f;
		f.trade_id = extract_string(obj, "trade_id");
		f.fill_id = extract_string(obj, "fill_id");
		f.order_id = extract_string(obj, "order_id");
		f.market_ticker = extract_string(obj, "ticker");
		if (f.market_ticker.empty()) {
			f.market_ticker = extract_string(obj, "market_ticker");
		}
		f.side = parse_side(extract_string(obj, "side"));
		f.action = parse_action(extract_string(obj, "action"));
		const std::string outcome_side = extract_string(obj, "outcome_side");
		f.outcome_side = outcome_side.empty()
						 ? derive_outcome_side(f.side, f.action)
						 : (outcome_side == "no" ? OutcomeSide::No : OutcomeSide::Yes);
		const std::string book_side = extract_string(obj, "book_side");
		f.book_side = book_side.empty() ? derive_book_side(f.side, f.action)
									 : (book_side == "ask" ? BookSide::Ask : BookSide::Bid);
		f.count_fp = extract_string(obj, "count_fp");
		f.yes_price_dollars = extract_string(obj, "yes_price_dollars");
		f.no_price_dollars = extract_string(obj, "no_price_dollars");
		f.fee_cost = extract_string(obj, "fee_cost");
		f.count = f.count_fp.empty() ? static_cast<std::int32_t>(extract_int(obj, "count"))
									 : exact_scaled_int_or_zero(f.count_fp, 0);
		f.yes_price = f.yes_price_dollars.empty()
						  ? static_cast<std::int32_t>(extract_int(obj, "yes_price"))
						  : exact_scaled_int_or_zero(f.yes_price_dollars, 2);
		f.no_price = f.no_price_dollars.empty()
						 ? static_cast<std::int32_t>(extract_int(obj, "no_price"))
						 : exact_scaled_int_or_zero(f.no_price_dollars, 2);
		f.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		f.created_time_iso = extract_string(obj, "created_time");
		f.created_time = f.created_time_iso.empty() ? extract_int(obj, "created_time")
													: extract_datetime(obj, "created_time");
		f.subaccount_number = extract_optional_int64(obj, "subaccount_number");
		f.timestamp = extract_int(obj, "ts");
		f.is_taker = extract_bool(obj, "is_taker");
		fills.push_back(f);
	}

	PaginatedResponse<Fill> result;
	result.items = std::move(fills);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

Result<PaginatedResponse<Settlement>>
KalshiClient::get_settlements(const GetSettlementsParams& params) {
	std::string path = "/portfolio/settlements";

	if (params.limit)
		append_query_param(path, "limit", *params.limit);
	if (params.cursor)
		append_query_param(path, "cursor", *params.cursor);
	if (params.market_ticker)
		append_query_param(path, "ticker", *params.market_ticker);
	if (params.event_ticker)
		append_query_param(path, "event_ticker", *params.event_ticker);
	if (params.min_ts)
		append_query_param(path, "min_ts", *params.min_ts);
	if (params.max_ts)
		append_query_param(path, "max_ts", *params.max_ts);
	if (params.subaccount)
		append_query_param(path, "subaccount", *params.subaccount);

	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get settlements: " + std::to_string(response->status_code),
				  response->status_code});
	}

	std::vector<Settlement> settlements;
	std::vector<std::string> settlement_objects =
		extract_array_objects(response->body, "settlements");

	for (const std::string& obj : settlement_objects) {
		Settlement s;
		s.market_ticker = extract_string(obj, "ticker");
		if (s.market_ticker.empty()) {
			s.market_ticker = extract_string(obj, "market_ticker");
		}
		s.event_ticker = extract_string(obj, "event_ticker");
		s.result = extract_string(obj, "market_result");
		if (s.result.empty()) {
			s.result = extract_string(obj, "result");
		}
		s.yes_count_fp = extract_string(obj, "yes_count_fp");
		s.no_count_fp = extract_string(obj, "no_count_fp");
		s.yes_count = s.yes_count_fp.empty()
						  ? static_cast<std::int32_t>(extract_int(obj, "yes_count"))
						  : exact_scaled_int_or_zero(s.yes_count_fp, 0);
		s.no_count = s.no_count_fp.empty() ? static_cast<std::int32_t>(extract_int(obj, "no_count"))
										   : exact_scaled_int_or_zero(s.no_count_fp, 0);
		s.yes_total_cost_dollars = extract_string(obj, "yes_total_cost_dollars");
		s.no_total_cost_dollars = extract_string(obj, "no_total_cost_dollars");
		s.fee_cost = extract_string(obj, "fee_cost");
		s.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		s.revenue = extract_int(obj, "revenue");
		s.settled_time_iso = extract_string(obj, "settled_time");
		s.settled_time = s.settled_time_iso.empty() ? extract_int(obj, "settled_time")
													  : extract_datetime(obj, "settled_time");
		s.value = extract_optional_int32(obj, "value");
		settlements.push_back(s);
	}

	PaginatedResponse<Settlement> result;
	result.items = std::move(settlements);

	std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}

	return result;
}

Result<PaginatedResponse<Settlement>>
KalshiClient::get_settlements(const GetPositionsParams& params) {
	GetSettlementsParams current;
	current.limit = params.limit;
	current.cursor = params.cursor;
	current.market_ticker = params.market_ticker;
	current.event_ticker = params.event_ticker;
	current.subaccount = params.subaccount;
	return get_settlements(current);
}

Result<PaginatedResponse<Deposit>>
KalshiClient::get_deposits(const GetPortfolioMovementParams& params) {
	std::string path = "/portfolio/deposits";
	if (params.limit)
		append_query_param(path, "limit", *params.limit);
	if (params.cursor)
		append_query_param(path, "cursor", *params.cursor);

	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get deposits: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<Deposit> result;
	result.items = api_detail::parse_deposits_response(response->body);
	const std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}
	return result;
}

Result<PaginatedResponse<Withdrawal>>
KalshiClient::get_withdrawals(const GetPortfolioMovementParams& params) {
	std::string path = "/portfolio/withdrawals";
	if (params.limit)
		append_query_param(path, "limit", *params.limit);
	if (params.cursor)
		append_query_param(path, "cursor", *params.cursor);

	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get withdrawals: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<Withdrawal> result;
	result.items = api_detail::parse_withdrawals_response(response->body);
	const std::string cursor = extract_cursor(response->body);
	if (!cursor.empty()) {
		result.next_cursor = Cursor{cursor};
	}
	return result;
}

// ===== Order Management =====

std::string KalshiClient::serialize_create_order(const CreateOrderParams& params) {
	// API requires stable key order — pinned by `glz::meta<ser::CreateOrderBody>`.
	return render_body(to_create_order_body(params));
}

Result<Order> KalshiClient::create_order(const CreateOrderParams& params) {
	const Result<void> valid = validate_create_order_v2(params);
	if (!valid) {
		return std::unexpected(valid.error());
	}
	std::string body = serialize_create_order(params);

	Result<HttpResponse> response = impl_->transport->post("/portfolio/events/orders", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to create order: " + response->body,
									 response->status_code});
	}

	return parse_order(response->body);
}

Result<void> KalshiClient::cancel_order(const std::string& order_id) {
	Result<HttpResponse> response = impl_->transport->del("/portfolio/events/orders/" + order_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to cancel order: " + response->body,
									 response->status_code});
	}

	return {};
}

Result<void> KalshiClient::cancel_all_orders(std::optional<std::int64_t> subaccount) {
	std::string path = "/portfolio/events/orders";
	if (subaccount) {
		append_query_param(path, "subaccount", *subaccount);
	}
	Result<HttpResponse> response = impl_->transport->del(path);
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to cancel all orders: " + response->body,
									 response->status_code});
	}
	return {};
}

Result<OrderCancelResult> KalshiClient::cancel_order_v2(const CancelOrderV2Params& params) {
	Result<HttpResponse> response =
		impl_->transport->del(api_detail::build_cancel_order_v2_path(params));
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to cancel event order: " + response->body,
									 response->status_code});
	}

	return api_detail::parse_order_cancel_result_response(response->body);
}

std::string KalshiClient::serialize_amend_order(const AmendOrderParams& params) {
	// API requires stable key order — pinned by `glz::meta<ser::AmendOrderBody>`.
	ser::AmendOrderBody body;
	body.ticker = params.ticker;
	body.side = params.book_side ? std::string(to_json_string(*params.book_side)) : std::string{};
	body.price = params.price_dollars.value_or(std::string{});
	body.count = params.count_fp.value_or(std::string{});
	body.client_order_id = params.client_order_id;
	body.updated_client_order_id = params.updated_client_order_id;
	body.exchange_index = params.exchange_index;
	return render_body(body);
}

Result<Order> KalshiClient::amend_order(const AmendOrderParams& params) {
	if (params.order_id.empty() || params.ticker.empty() || !params.book_side ||
		!params.price_dollars || !params.count_fp || !FixedPoint::parse(*params.price_dollars) ||
		!FixedPoint::parse(*params.count_fp)) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest,
				  "V2 amend requires order_id, ticker, book_side, price_dollars, and count_fp"});
	}
	std::string body = serialize_amend_order(params);

	std::string path = "/portfolio/events/orders/" + params.order_id + "/amend";
	if (params.subaccount) {
		append_query_param(path, "subaccount", *params.subaccount);
	}
	Result<HttpResponse> response = impl_->transport->post(path, body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to amend order: " + response->body,
									 response->status_code});
	}

	return parse_order(response->body);
}

std::string KalshiClient::serialize_decrease_order(const DecreaseOrderParams& params) {
	ser::DecreaseOrderBody body;
	body.reduce_by = params.reduce_by_fp;
	body.reduce_to = params.reduce_to_fp;
	body.exchange_index = params.exchange_index;
	body.market_ticker = params.market_ticker;
	return render_body(body);
}

Result<Order> KalshiClient::decrease_order(const DecreaseOrderParams& params) {
	if (params.order_id.empty() ||
		params.reduce_by_fp.has_value() == params.reduce_to_fp.has_value() ||
		(params.reduce_by_fp && !FixedPoint::parse(*params.reduce_by_fp)) ||
		(params.reduce_to_fp && !FixedPoint::parse(*params.reduce_to_fp))) {
		return std::unexpected(Error{
			ErrorCode::InvalidRequest,
			"V2 decrease requires order_id and exactly one exact reduce_by_fp or reduce_to_fp"});
	}
	std::string body = serialize_decrease_order(params);

	std::string path = "/portfolio/events/orders/" + params.order_id + "/decrease";
	if (params.subaccount) {
		append_query_param(path, "subaccount", *params.subaccount);
	}
	Result<HttpResponse> response = impl_->transport->post(path, body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to decrease order: " + response->body,
									 response->status_code});
	}

	return parse_order(response->body);
}

std::string KalshiClient::serialize_batch_create(const BatchOrderRequest& request) {
	// API requires stable key order in embedded order objects — pinned by
	// `glz::meta<ser::CreateOrderBody>`. The pre-migration impl serialized
	// each order to a string and parsed it back into an ordered_json node;
	// here we just build the same struct vector and let Glaze emit it.
	ser::BatchOrdersBody body;
	body.orders.reserve(request.orders.size());
	for (const CreateOrderParams& order : request.orders) {
		body.orders.push_back(to_create_order_body(order));
	}
	return render_body(body);
}

Result<BatchResponse<Order>> KalshiClient::batch_create_orders(const BatchOrderRequest& request) {
	if (request.orders.empty()) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "batch create requires at least one order"});
	}
	for (const CreateOrderParams& order : request.orders) {
		const Result<void> valid = validate_create_order_v2(order);
		if (!valid)
			return std::unexpected(valid.error());
	}
	std::string body = serialize_batch_create(request);

	Result<HttpResponse> response =
		impl_->transport->post("/portfolio/events/orders/batched", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to batch create orders: " + response->body,
									 response->status_code});
	}

	BatchResponse<Order> result;
	Result<std::vector<Order>> orders = parse_orders(response->body);
	if (orders) {
		result.results = std::move(*orders);
	}

	return result;
}

std::string KalshiClient::serialize_batch_cancel(const BatchCancelRequest& request) {
	ser::BatchCancelBody body;
	if (!request.orders.empty()) {
		body.orders.reserve(request.orders.size());
		for (const BatchCancelOrder& order : request.orders) {
			body.orders.push_back(to_batch_cancel_order_body(order));
		}
	} else {
		body.orders.reserve(request.order_ids.size());
		for (const std::string& order_id : request.order_ids) {
			body.orders.push_back(ser::BatchCancelOrderBody{.order_id = order_id});
		}
	}
	return render_body(body);
}

Result<BatchResponse<std::string>>
KalshiClient::batch_cancel_orders(const BatchCancelRequest& request) {
	std::string body = serialize_batch_cancel(request);

	Result<HttpResponse> response = impl_->transport->del("/portfolio/events/orders/batched", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to batch cancel orders: " + response->body,
									 response->status_code});
	}

	BatchResponse<std::string> result;
	result.results = batch_cancel_result_ids(request); // Assume all requested IDs cancelled if 200
	return result;
}

Result<BatchResponse<OrderCancelResult>>
KalshiClient::batch_cancel_orders_v2(const BatchCancelRequest& request) {
	std::string body = serialize_batch_cancel(request);

	Result<HttpResponse> response = impl_->transport->del("/portfolio/events/orders/batched", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to batch cancel event orders: " + response->body,
									 response->status_code});
	}

	BatchResponse<OrderCancelResult> result;
	result.results = api_detail::parse_batch_order_cancel_result_response(response->body);
	for (const OrderCancelResult& order : result.results) {
		if (order.error && !order.error->message.empty()) {
			result.errors.push_back(order.error->message);
		}
	}
	return result;
}

// ===== Phase 1: Exchange API (Schedule, Announcements) =====

Result<Schedule> KalshiClient::get_exchange_schedule() {
	Result<HttpResponse> response = impl_->transport->get("/exchange/schedule");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get exchange schedule: " + std::to_string(response->status_code),
				  response->status_code});
	}

	Schedule schedule;
	const auto parse_daily_schedules = [](const std::string& json, const std::string& day) {
		std::vector<DailySchedule> sessions;
		for (const std::string& obj : extract_array_objects(json, day)) {
			sessions.push_back(DailySchedule{.open_time = extract_string(obj, "open_time"),
											 .close_time = extract_string(obj, "close_time")});
		}
		return sessions;
	};

	// Parse standard_hours array
	std::vector<std::string> hours_objects =
		extract_array_objects(response->body, "standard_hours");
	for (const std::string& obj : hours_objects) {
		WeeklySchedule ws;
		ws.start_time = extract_string(obj, "start_time");
		ws.end_time = extract_string(obj, "end_time");
		ws.monday = parse_daily_schedules(obj, "monday");
		ws.tuesday = parse_daily_schedules(obj, "tuesday");
		ws.wednesday = parse_daily_schedules(obj, "wednesday");
		ws.thursday = parse_daily_schedules(obj, "thursday");
		ws.friday = parse_daily_schedules(obj, "friday");
		ws.saturday = parse_daily_schedules(obj, "saturday");
		ws.sunday = parse_daily_schedules(obj, "sunday");
		ws.day = extract_string(obj, "day");
		ws.open = extract_string(obj, "open");
		ws.close = extract_string(obj, "close");
		schedule.standard_hours.push_back(ws);
	}

	// Parse maintenance_windows array
	std::vector<std::string> maint_objects =
		extract_array_objects(response->body, "maintenance_windows");
	for (const std::string& obj : maint_objects) {
		MaintenanceWindow mw;
		mw.start_datetime = extract_string(obj, "start_datetime");
		mw.end_datetime = extract_string(obj, "end_datetime");
		mw.start = extract_int(obj, "start");
		mw.end = extract_int(obj, "end");
		mw.description = extract_string(obj, "description");
		schedule.maintenance_windows.push_back(mw);
	}

	return schedule;
}

Result<std::vector<Announcement>> KalshiClient::get_exchange_announcements() {
	return std::unexpected(
		Error{ErrorCode::InvalidRequest, "exchange announcements were removed upstream"});
}

// ===== Phase 2: Events/Series API =====

Result<EventMetadata> KalshiClient::get_event_metadata(const std::string& event_ticker) {
	Result<HttpResponse> response = impl_->transport->get("/events/" + event_ticker + "/metadata");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get event metadata: " + std::to_string(response->status_code),
				  response->status_code});
	}

	EventMetadata metadata;
	metadata.image_url = extract_string(response->body, "image_url");
	metadata.featured_image_url = extract_string(response->body, "featured_image_url");
	metadata.competition = extract_string(response->body, "competition");
	metadata.competition_scope = extract_string(response->body, "competition_scope");
	for (const std::string& obj : extract_array_objects(response->body, "market_details")) {
		metadata.market_details.push_back(
			EventMetadata::MarketDetail{.market_ticker = extract_string(obj, "market_ticker"),
										.image_url = extract_string(obj, "image_url"),
										.color_code = extract_string(obj, "color_code")});
	}
	for (const std::string& obj : extract_array_objects(response->body, "settlement_sources")) {
		metadata.settlement_sources.push_back(SettlementSource{.name = extract_string(obj, "name"),
															   .url = extract_string(obj, "url")});
	}
	return metadata;
}

std::string KalshiClient::build_series_query(const GetSeriesParams& params) {
	return api_detail::build_series_query_string(params);
}

Result<PaginatedResponse<Series>> KalshiClient::get_series_list(const GetSeriesParams& params) {
	if (params.limit || params.cursor) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "series pagination was removed upstream"});
	}
	std::string query = build_series_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get series list: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<Series> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects = extract_array_objects(response->body, "series");
	for (const std::string& obj : objects) {
		Series s;
		s.ticker = extract_string(obj, "ticker");
		s.title = extract_string(obj, "title");
		s.category = extract_string(obj, "category");
		s.frequency = extract_string(obj, "frequency");
		s.tags = extract_string_array(obj, "tags");
		s.contract_url = extract_string(obj, "contract_url");
		s.contract_terms_url = extract_string(obj, "contract_terms_url");
		s.fee_type = extract_string(obj, "fee_type");
		s.fee_multiplier = extract_double(obj, "fee_multiplier");
		s.additional_prohibitions = extract_string_array(obj, "additional_prohibitions");
		s.volume_fp = extract_string(obj, "volume_fp");
		s.last_updated_ts = extract_string(obj, "last_updated_ts");
		s.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		for (const std::string& source : extract_array_objects(obj, "settlement_sources")) {
			s.settlement_source_details.push_back(SettlementSource{
				.name = extract_string(source, "name"), .url = extract_string(source, "url")});
		}
		result.items.push_back(s);
	}

	return result;
}

// ===== Phase 3: Order Groups =====

std::string KalshiClient::serialize_order_group(const CreateOrderGroupParams& params) {
	// API requires stable key order — pinned by `glz::meta<ser::OrderGroupBody>`.
	ser::OrderGroupBody body;
	body.subaccount = params.subaccount;
	body.contracts_limit = params.contracts_limit;
	body.contracts_limit_fp = params.contracts_limit_fp;
	body.exchange_index = params.exchange_index;
	return render_body(body);
}

std::string KalshiClient::build_order_groups_query(const GetOrderGroupsParams& params) {
	std::string query = "/portfolio/order_groups";
	if (params.subaccount)
		append_query_param(query, "subaccount", *params.subaccount);
	return query;
}

Result<OrderGroup> KalshiClient::create_order_group(const CreateOrderGroupParams& params) {
	if (!params.type.empty() || !params.order_ids.empty() ||
		(params.contracts_limit_fp && !FixedPoint::parse(*params.contracts_limit_fp))) {
		return std::unexpected(Error{ErrorCode::InvalidRequest,
									 "order group request does not satisfy the current schema"});
	}
	std::string body = serialize_order_group(params);
	Result<HttpResponse> response = impl_->transport->post("/portfolio/order_groups/create", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to create order group: " + response->body,
									 response->status_code});
	}

	OrderGroup group;
	group.id = extract_string(response->body, "order_group_id");
	group.subaccount = extract_int(response->body, "subaccount");
	group.exchange_index = static_cast<std::int32_t>(extract_int(response->body, "exchange_index"));
	group.order_ids = extract_string_array(response->body, "orders");
	return group;
}

Result<PaginatedResponse<OrderGroup>>
KalshiClient::get_order_groups(const GetOrderGroupsParams& params) {
	if (params.limit || params.cursor || params.status) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "order group list filters were removed upstream"});
	}
	std::string query = build_order_groups_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get order groups: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<OrderGroup> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects = extract_array_objects(response->body, "order_groups");
	for (const std::string& obj : objects) {
		OrderGroup g;
		g.id = extract_string(obj, "id");
		g.contracts_limit_fp = extract_string(obj, "contracts_limit_fp");
		g.is_auto_cancel_enabled = extract_bool(obj, "is_auto_cancel_enabled");
		g.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		result.items.push_back(g);
	}

	return result;
}

Result<OrderGroup> KalshiClient::get_order_group(const std::string& group_id) {
	return get_order_group(group_id, OrderGroupSelector{.subaccount = 0});
}

Result<OrderGroup> KalshiClient::get_order_group(const std::string& group_id,
													 const OrderGroupSelector& selector) {
	std::string path = "/portfolio/order_groups/" + group_id;
	if (selector.subaccount) {
		append_query_param(path, "subaccount", *selector.subaccount);
	}
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get order group: " + std::to_string(response->status_code),
				  response->status_code});
	}

	OrderGroup group;
	group.id = group_id;
	group.contracts_limit_fp = extract_string(response->body, "contracts_limit_fp");
	group.is_auto_cancel_enabled = extract_bool(response->body, "is_auto_cancel_enabled");
	group.exchange_index = static_cast<std::int32_t>(extract_int(response->body, "exchange_index"));
	group.order_ids = extract_string_array(response->body, "orders");
	return group;
}

Result<void> KalshiClient::delete_order_group(const std::string& group_id) {
	return delete_order_group(group_id, OrderGroupSelector{.subaccount = 0});
}

Result<void> KalshiClient::delete_order_group(const std::string& group_id,
												const OrderGroupSelector& selector) {
	std::string path = "/portfolio/order_groups/" + group_id;
	if (selector.subaccount) {
		append_query_param(path, "subaccount", *selector.subaccount);
	}
	if (selector.exchange_index) {
		append_query_param(path, "exchange_index", *selector.exchange_index);
	}
	Result<HttpResponse> response = impl_->transport->del(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to delete order group: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return {};
}

Result<OrderGroup> KalshiClient::reset_order_group(const std::string& group_id) {
	return reset_order_group(group_id, OrderGroupSelector{.subaccount = 0});
}

Result<OrderGroup> KalshiClient::reset_order_group(const std::string& group_id,
													   const OrderGroupSelector& selector) {
	std::string path = "/portfolio/order_groups/" + group_id + "/reset";
	if (selector.subaccount) {
		append_query_param(path, "subaccount", *selector.subaccount);
	}
	if (selector.exchange_index) {
		append_query_param(path, "exchange_index", *selector.exchange_index);
	}
	Result<HttpResponse> response = impl_->transport->put(path, "{}");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to reset order group: " + std::to_string(response->status_code),
				  response->status_code});
	}

	OrderGroup group;
	group.id = group_id;
	return group;
}

// ===== Phase 4: Order Queue Position =====

Result<OrderQueuePosition> KalshiClient::get_order_queue_position(const std::string& order_id) {
	Result<HttpResponse> response =
		impl_->transport->get("/portfolio/orders/" + order_id + "/queue_position");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get queue position: " + std::to_string(response->status_code),
				  response->status_code});
	}

	OrderQueuePosition pos;
	pos.order_id = order_id;
	pos.queue_position_fp = extract_string(response->body, "queue_position_fp");
	pos.position = pos.queue_position_fp.empty()
					   ? static_cast<std::int32_t>(extract_int(response->body, "position"))
					   : exact_scaled_int_or_zero(pos.queue_position_fp, 0);
	pos.total_at_price = static_cast<std::int32_t>(extract_int(response->body, "total_at_price"));
	return pos;
}

std::string KalshiClient::serialize_order_ids(const std::vector<std::string>& order_ids) {
	ser::OrderIdsBody body;
	body.order_ids = order_ids;
	return render_body(body);
}

Result<std::vector<OrderQueuePosition>>
KalshiClient::get_queue_positions(const std::vector<std::string>& order_ids) {
	(void)order_ids;
	return std::unexpected(Error{
		ErrorCode::InvalidRequest,
		"queue position lookup by order ID was removed; filter by market_tickers or event_ticker"});
}

Result<std::vector<OrderQueuePosition>>
KalshiClient::get_queue_positions(const GetQueuePositionsParams& params) {
	std::string path = "/portfolio/orders/queue_positions";
	if (params.market_tickers)
		append_query_param(path, "market_tickers", *params.market_tickers);
	if (params.event_ticker)
		append_query_param(path, "event_ticker", *params.event_ticker);
	if (params.subaccount)
		append_query_param(path, "subaccount", *params.subaccount);
	Result<HttpResponse> response = impl_->transport->get(path);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get queue positions: " + std::to_string(response->status_code),
				  response->status_code});
	}

	std::vector<OrderQueuePosition> positions;
	std::vector<std::string> objects = extract_array_objects(response->body, "queue_positions");
	for (const std::string& obj : objects) {
		OrderQueuePosition pos;
		pos.order_id = extract_string(obj, "order_id");
		pos.market_ticker = extract_string(obj, "market_ticker");
		pos.queue_position_fp = extract_string(obj, "queue_position_fp");
		pos.position = pos.queue_position_fp.empty()
						   ? static_cast<std::int32_t>(extract_int(obj, "position"))
						   : exact_scaled_int_or_zero(pos.queue_position_fp, 0);
		pos.total_at_price = static_cast<std::int32_t>(extract_int(obj, "total_at_price"));
		positions.push_back(pos);
	}

	return positions;
}

// ===== Phase 5: RFQ/Quotes =====

std::string KalshiClient::serialize_rfq(const CreateRfqParams& params) {
	// API requires stable key order — pinned by `glz::meta<ser::RfqBody>`.
	ser::RfqBody body;
	body.market_ticker = params.market_ticker;
	body.contracts_fp = params.contracts_fp;
	if (!body.contracts_fp && params.count > 0) {
		body.contracts_fp = std::to_string(params.count);
	}
	body.target_cost_dollars = params.target_cost_dollars;
	body.rest_remainder = params.rest_remainder;
	body.replace_existing = params.replace_existing;
	body.subtrader_id = params.subtrader_id;
	body.subaccount = params.subaccount;
	return render_body(body);
}

std::string KalshiClient::build_rfqs_query(const GetRfqsParams& params) {
	std::string query = "/communications/rfqs";
	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.event_ticker)
		append_query_param(query, "event_ticker", *params.event_ticker);
	if (params.market_ticker)
		append_query_param(query, "market_ticker", *params.market_ticker);
	if (params.subaccount)
		append_query_param(query, "subaccount", *params.subaccount);
	if (params.status)
		append_query_param(query, "status", *params.status);
	if (params.user_filter)
		append_query_param(query, "user_filter", *params.user_filter);
	return query;
}

Result<Rfq> KalshiClient::create_rfq(const CreateRfqParams& params) {
	if (params.market_ticker.empty() || params.expires_at ||
		(params.contracts_fp && !FixedPoint::parse(*params.contracts_fp)) ||
		(params.target_cost_dollars && !FixedPoint::parse(*params.target_cost_dollars))) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "RFQ request does not satisfy the current schema"});
	}
	std::string body = serialize_rfq(params);
	Result<HttpResponse> response = impl_->transport->post("/communications/rfqs", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to create RFQ: " + response->body,
									 response->status_code});
	}

	return parse_rfq_response(response->body);
}

Result<PaginatedResponse<Rfq>> KalshiClient::get_rfqs(const GetRfqsParams& params) {
	std::string query = build_rfqs_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to get RFQs: " + std::to_string(response->status_code),
									 response->status_code});
	}

	PaginatedResponse<Rfq> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects = extract_array_objects(response->body, "rfqs");
	for (const std::string& obj : objects) {
		result.items.push_back(parse_rfq_response(obj));
	}

	return result;
}

Result<Rfq> KalshiClient::get_rfq(const std::string& rfq_id) {
	Result<HttpResponse> response = impl_->transport->get("/communications/rfqs/" + rfq_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to get RFQ: " + std::to_string(response->status_code),
									 response->status_code});
	}

	return parse_rfq_response(response->body);
}

std::string KalshiClient::serialize_quote(const CreateQuoteParams& params) {
	// API requires stable key order — pinned by `glz::meta<ser::QuoteBody>`.
	ser::QuoteBody body;
	body.rfq_id = params.rfq_id;
	body.yes_bid = params.yes_bid_dollars;
	body.no_bid = params.no_bid_dollars;
	body.rest_remainder = params.rest_remainder;
	body.post_only = params.post_only;
	body.subaccount = params.subaccount;
	return render_body(body);
}

std::string KalshiClient::build_quotes_query(const GetQuotesParams& params) {
	std::string query = "/communications/quotes";
	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.min_ts)
		append_query_param(query, "min_ts", *params.min_ts);
	if (params.max_ts)
		append_query_param(query, "max_ts", *params.max_ts);
	if (params.rfq_id)
		append_query_param(query, "rfq_id", *params.rfq_id);
	if (params.status)
		append_query_param(query, "status", *params.status);
	if (params.user_filter)
		append_query_param(query, "user_filter", *params.user_filter);
	if (params.rfq_user_filter)
		append_query_param(query, "rfq_user_filter", *params.rfq_user_filter);
	if (params.rfq_creator_subtrader_id)
		append_query_param(query, "rfq_creator_subtrader_id", *params.rfq_creator_subtrader_id);
	return query;
}

Result<Quote> KalshiClient::create_quote(const CreateQuoteParams& params) {
	if (params.rfq_id.empty() || !FixedPoint::parse(params.yes_bid_dollars) ||
		!FixedPoint::parse(params.no_bid_dollars) || params.price != 0 || params.count != 0 ||
		params.expires_at) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "quote request does not satisfy the current schema"});
	}
	std::string body = serialize_quote(params);
	Result<HttpResponse> response = impl_->transport->post("/communications/quotes", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to create quote: " + response->body,
									 response->status_code});
	}

	return parse_quote_response(response->body);
}

Result<PaginatedResponse<Quote>> KalshiClient::get_quotes(const GetQuotesParams& params) {
	std::string query = build_quotes_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get quotes: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<Quote> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects = extract_array_objects(response->body, "quotes");
	for (const std::string& obj : objects) {
		result.items.push_back(parse_quote_response(obj));
	}

	return result;
}

Result<Quote> KalshiClient::get_quote(const std::string& quote_id) {
	Result<HttpResponse> response = impl_->transport->get("/communications/quotes/" + quote_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{
			ErrorCode::ServerError, "Failed to get quote: " + std::to_string(response->status_code),
			response->status_code});
	}

	return parse_quote_response(response->body);
}

Result<Quote> KalshiClient::get_quote(const std::string& rfq_id, const std::string& quote_id) {
	Result<HttpResponse> response = impl_->transport->get("/communications/rfqs/" + rfq_id +
													 "/quotes/" + quote_id);
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200) {
		return std::unexpected(Error{
			ErrorCode::ServerError, "Failed to get quote: " + std::to_string(response->status_code),
			response->status_code});
	}
	return parse_quote_response(response->body);
}

Result<void> KalshiClient::accept_quote(const std::string& quote_id) {
	(void)quote_id;
	return std::unexpected(Error{ErrorCode::InvalidRequest, "accepted_side is required"});
}

Result<void> KalshiClient::accept_quote(const std::string& quote_id, Side accepted_side) {
	ser::AcceptQuoteBody body{.accepted_side = std::string(to_json_string(accepted_side))};
	Result<HttpResponse> response =
		impl_->transport->put("/communications/quotes/" + quote_id + "/accept", render_body(body));
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to accept quote: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return {};
}

Result<void> KalshiClient::accept_quote(const std::string& rfq_id, const std::string& quote_id,
										Side accepted_side) {
	ser::AcceptQuoteBody body{.accepted_side = std::string(to_json_string(accepted_side))};
	Result<HttpResponse> response = impl_->transport->put(
		"/communications/rfqs/" + rfq_id + "/quotes/" + quote_id + "/accept", render_body(body));
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to accept quote: " + std::to_string(response->status_code),
				  response->status_code});
	}
	return {};
}

// ===== Phase 6: Administrative Endpoints =====

Result<ApiKeysResponse> KalshiClient::get_api_keys_response() {
	Result<HttpResponse> response = impl_->transport->get("/api_keys");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get API keys: " + std::to_string(response->status_code),
				  response->status_code});
	}

	ApiKeysResponse result;
	std::vector<std::string> objects = extract_array_objects(response->body, "api_keys");
	for (const std::string& obj : objects) {
		ApiKey key;
		key.id = extract_string(obj, "api_key_id");
		key.name = extract_string(obj, "name");
		key.scopes = extract_string_array(obj, "scopes");
		key.subaccount = extract_optional_int64(obj, "subaccount");
		key.fcm_subtrader_id = extract_string(obj, "fcm_subtrader_id");
		key.created_time = extract_int(obj, "created_time");
		result.api_keys.push_back(std::move(key));
	}
	result.api_key_region_expiration_ts =
		extract_optional_int64(response->body, "api_key_region_expiration_ts");

	return result;
}

Result<std::vector<ApiKey>> KalshiClient::get_api_keys() {
	Result<ApiKeysResponse> response = get_api_keys_response();
	if (!response) {
		return std::unexpected(response.error());
	}
	return std::move(response->api_keys);
}

std::string KalshiClient::serialize_api_key(const CreateApiKeyParams& params) {
	// API requires stable key order — pinned by `glz::meta<ser::ApiKeyBody>`.
	ser::ApiKeyBody body;
	body.name = params.name;
	body.public_key = params.public_key;
	if (!params.scopes.empty()) {
		body.scopes = params.scopes;
	}
	body.subaccount = params.subaccount;
	body.fcm_subtrader_id = params.fcm_subtrader_id;
	return render_body(body);
}

Result<ApiKey> KalshiClient::create_api_key(const CreateApiKeyParams& params) {
	if (params.name.empty() || params.public_key.empty() || params.expires_at ||
		(params.subaccount && params.fcm_subtrader_id)) {
		return std::unexpected(Error{ErrorCode::InvalidRequest,
									 "API key request does not satisfy the current schema"});
	}
	std::string body = serialize_api_key(params);
	Result<HttpResponse> response = impl_->transport->post("/api_keys", body);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to create API key: " + response->body,
									 response->status_code});
	}

	ApiKey key;
	key.id = extract_string(response->body, "api_key_id");
	key.warning = extract_string(response->body, "warning");
	key.name = extract_string(response->body, "name");
	key.created_time = extract_int(response->body, "created_time");
	return key;
}

Result<void> KalshiClient::delete_api_key(const std::string& key_id) {
	Result<HttpResponse> response = impl_->transport->del("/api_keys/" + key_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to delete API key: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return {};
}

std::string KalshiClient::build_milestones_query(const GetMilestonesParams& params) {
	std::string query = "/milestones";
	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.minimum_start_date)
		append_query_param(query, "minimum_start_date", *params.minimum_start_date);
	if (params.category)
		append_query_param(query, "category", *params.category);
	if (params.competition)
		append_query_param(query, "competition", *params.competition);
	if (params.source_id)
		append_query_param(query, "source_id", *params.source_id);
	if (params.type)
		append_query_param(query, "type", *params.type);
	if (params.related_event_ticker)
		append_query_param(query, "related_event_ticker", *params.related_event_ticker);
	else if (params.event_ticker)
		append_query_param(query, "related_event_ticker", *params.event_ticker);
	if (params.min_updated_ts)
		append_query_param(query, "min_updated_ts", *params.min_updated_ts);
	return query;
}

Result<PaginatedResponse<Milestone>>
KalshiClient::get_milestones(const GetMilestonesParams& params) {
	std::string query = build_milestones_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get milestones: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<Milestone> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects = extract_array_objects(response->body, "milestones");
	for (const std::string& obj : objects) {
		Milestone m;
		m.id = extract_string(obj, "id");
		m.category = extract_string(obj, "category");
		m.type = extract_string(obj, "type");
		m.start_date = extract_string(obj, "start_date");
		m.end_date = extract_string(obj, "end_date");
		m.title = extract_string(obj, "title");
		m.notification_message = extract_string(obj, "notification_message");
		m.source_id = extract_string(obj, "source_id");
		m.last_updated_ts = extract_string(obj, "last_updated_ts");
		m.related_event_tickers = extract_string_array(obj, "related_event_tickers");
		m.primary_event_tickers = extract_string_array(obj, "primary_event_tickers");
		result.items.push_back(m);
	}

	return result;
}

Result<Milestone> KalshiClient::get_milestone(const std::string& milestone_id) {
	Result<HttpResponse> response = impl_->transport->get("/milestones/" + milestone_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get milestone: " + std::to_string(response->status_code),
				  response->status_code});
	}

	Milestone m;
	m.id = extract_string(response->body, "id");
	m.category = extract_string(response->body, "category");
	m.type = extract_string(response->body, "type");
	m.start_date = extract_string(response->body, "start_date");
	m.end_date = extract_string(response->body, "end_date");
	m.title = extract_string(response->body, "title");
	m.notification_message = extract_string(response->body, "notification_message");
	m.source_id = extract_string(response->body, "source_id");
	m.last_updated_ts = extract_string(response->body, "last_updated_ts");
	m.related_event_tickers = extract_string_array(response->body, "related_event_tickers");
	m.primary_event_tickers = extract_string_array(response->body, "primary_event_tickers");
	return m;
}

std::string KalshiClient::build_multivariate_query(const GetMultivariateCollectionsParams& params) {
	std::string query = "/multivariate_event_collections";
	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.status)
		append_query_param(query, "status", *params.status);
	if (params.associated_event_ticker)
		append_query_param(query, "associated_event_ticker", *params.associated_event_ticker);
	if (params.series_ticker)
		append_query_param(query, "series_ticker", *params.series_ticker);
	return query;
}

Result<PaginatedResponse<MultivariateCollection>>
KalshiClient::get_multivariate_collections(const GetMultivariateCollectionsParams& params) {
	std::string query = build_multivariate_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to get multivariate collections: " +
										 std::to_string(response->status_code),
									 response->status_code});
	}

	PaginatedResponse<MultivariateCollection> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects =
		extract_array_objects(response->body, "multivariate_contracts");
	for (const std::string& obj : objects) {
		MultivariateCollection c;
		c.collection_ticker = extract_string(obj, "collection_ticker");
		c.id = c.collection_ticker;
		c.series_ticker = extract_string(obj, "series_ticker");
		c.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		c.title = extract_string(obj, "title");
		c.description = extract_string(obj, "description");
		c.open_date = extract_string(obj, "open_date");
		c.close_date = extract_string(obj, "close_date");
		c.is_ordered = extract_bool(obj, "is_ordered");
		c.is_single_market_per_event = extract_bool(obj, "is_single_market_per_event");
		c.is_all_yes = extract_bool(obj, "is_all_yes");
		c.size_min = static_cast<std::int32_t>(extract_int(obj, "size_min"));
		c.size_max = static_cast<std::int32_t>(extract_int(obj, "size_max"));
		c.functional_description = extract_string(obj, "functional_description");
		result.items.push_back(c);
	}

	return result;
}

Result<MultivariateCollection>
KalshiClient::get_multivariate_collection(const std::string& collection_id) {
	Result<HttpResponse> response =
		impl_->transport->get("/multivariate_event_collections/" + collection_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get multivariate collection: " + std::to_string(response->status_code),
				  response->status_code});
	}

	MultivariateCollection c;
	c.collection_ticker = extract_string(response->body, "collection_ticker");
	c.id = c.collection_ticker;
	c.series_ticker = extract_string(response->body, "series_ticker");
	c.exchange_index = static_cast<std::int32_t>(extract_int(response->body, "exchange_index"));
	c.title = extract_string(response->body, "title");
	c.description = extract_string(response->body, "description");
	c.open_date = extract_string(response->body, "open_date");
	c.close_date = extract_string(response->body, "close_date");
	c.is_ordered = extract_bool(response->body, "is_ordered");
	c.is_single_market_per_event = extract_bool(response->body, "is_single_market_per_event");
	c.is_all_yes = extract_bool(response->body, "is_all_yes");
	c.size_min = static_cast<std::int32_t>(extract_int(response->body, "size_min"));
	c.size_max = static_cast<std::int32_t>(extract_int(response->body, "size_max"));
	c.functional_description = extract_string(response->body, "functional_description");
	return c;
}

std::string KalshiClient::build_structured_targets_query(const GetStructuredTargetsParams& params) {
	std::string query = "/structured_targets";
	if (params.page_size)
		append_query_param(query, "page_size", *params.page_size);
	else if (params.limit)
		append_query_param(query, "page_size", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	if (params.ids)
		append_query_param(query, "ids", *params.ids);
	if (params.type)
		append_query_param(query, "type", *params.type);
	if (params.competition)
		append_query_param(query, "competition", *params.competition);
	return query;
}

Result<PaginatedResponse<StructuredTarget>>
KalshiClient::get_structured_targets(const GetStructuredTargetsParams& params) {
	std::string query = build_structured_targets_query(params);
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get structured targets: " + std::to_string(response->status_code),
				  response->status_code});
	}

	PaginatedResponse<StructuredTarget> result;
	result.next_cursor = Cursor{extract_cursor(response->body)};

	std::vector<std::string> objects = extract_array_objects(response->body, "structured_targets");
	for (const std::string& obj : objects) {
		StructuredTarget t;
		t.id = extract_string(obj, "id");
		t.name = extract_string(obj, "name");
		t.type = extract_string(obj, "type");
		t.source_id = extract_string(obj, "source_id");
		t.last_updated_ts = extract_string(obj, "last_updated_ts");
		t.title = t.name;
		t.target_type = t.type;
		result.items.push_back(t);
	}

	return result;
}

Result<StructuredTarget> KalshiClient::get_structured_target(const std::string& target_id) {
	Result<HttpResponse> response = impl_->transport->get("/structured_targets/" + target_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get structured target: " + std::to_string(response->status_code),
				  response->status_code});
	}

	StructuredTarget t;
	t.id = extract_string(response->body, "id");
	t.name = extract_string(response->body, "name");
	t.type = extract_string(response->body, "type");
	t.source_id = extract_string(response->body, "source_id");
	t.last_updated_ts = extract_string(response->body, "last_updated_ts");
	t.title = t.name;
	t.target_type = t.type;
	return t;
}

Result<Communication> KalshiClient::get_communication(const std::string& comm_id) {
	(void)comm_id;
	return std::unexpected(
		Error{ErrorCode::InvalidRequest, "generic communications lookup was removed upstream"});
}

// ===== Phase 7: Search, Live Data, Incentive Programs =====

std::string KalshiClient::build_search_query(const SearchParams& params) {
	std::string query;
	append_query_param(query, "query", params.query);
	if (params.limit)
		append_query_param(query, "limit", *params.limit);
	if (params.cursor)
		append_query_param(query, "cursor", *params.cursor);
	return query;
}

Result<PaginatedResponse<Event>> KalshiClient::search_events(const SearchParams& params) {
	(void)params;
	return std::unexpected(Error{ErrorCode::InvalidRequest, "event search was removed upstream"});
}

Result<PaginatedResponse<Market>> KalshiClient::search_markets(const SearchParams& params) {
	(void)params;
	return std::unexpected(Error{ErrorCode::InvalidRequest, "market search was removed upstream"});
}

Result<LiveData> KalshiClient::get_live_data(const std::string& ticker) {
	(void)ticker;
	return std::unexpected(
		Error{ErrorCode::InvalidRequest, "generic live-data ticker lookup was removed upstream"});
}

std::string KalshiClient::serialize_tickers(const std::vector<std::string>& tickers) {
	ser::TickersBody body;
	body.tickers = tickers;
	return render_body(body);
}

Result<std::vector<LiveData>>
KalshiClient::get_live_datas(const std::vector<std::string>& tickers) {
	(void)tickers;
	return std::unexpected(Error{
		ErrorCode::InvalidRequest,
		"generic live-data batch model was removed; use the current typed live_data contract"});
}

Result<std::vector<IncentiveProgram>> KalshiClient::get_incentive_programs() {
	Result<HttpResponse> response = impl_->transport->get("/incentive_programs");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get incentive programs: " + std::to_string(response->status_code),
				  response->status_code});
	}

	std::vector<IncentiveProgram> programs;
	std::vector<std::string> objects = extract_array_objects(response->body, "incentive_programs");
	for (const std::string& obj : objects) {
		IncentiveProgram p;
		p.id = extract_string(obj, "id");
		p.market_id = extract_string(obj, "market_id");
		p.market_ticker = extract_string(obj, "market_ticker");
		p.incentive_type = extract_string(obj, "incentive_type");
		p.incentive_description = extract_string(obj, "incentive_description");
		p.start_date = extract_string(obj, "start_date");
		p.end_date = extract_string(obj, "end_date");
		p.period_reward = extract_int(obj, "period_reward");
		p.paid_out = extract_bool(obj, "paid_out");
		p.target_size_fp = extract_string(obj, "target_size_fp");
		p.title = p.market_ticker;
		p.description = p.incentive_description;
		programs.push_back(p);
	}

	return programs;
}

// ===== Additional endpoints for full SDK parity =====

Result<TotalRestingOrderValue> KalshiClient::get_total_resting_order_value() {
	Result<HttpResponse> response =
		impl_->transport->get("/portfolio/summary/total_resting_order_value");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to get total resting order value: " +
										 std::to_string(response->status_code),
									 response->status_code});
	}

	TotalRestingOrderValue result;
	result.total_value = extract_int(response->body, "total_value");
	return result;
}

// ===== Subaccounts =====

Result<Subaccount> KalshiClient::create_subaccount() {
	// Kalshi's create-subaccount endpoint takes no body — POST with
	// an empty payload returns the new subaccount_number + initial
	// balance (always 0 on creation).
	Result<HttpResponse> response = impl_->transport->post("/portfolio/subaccounts", "");
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to create subaccount: " + response->body,
									 response->status_code});
	}
	Subaccount sub;
	sub.subaccount_number = extract_int(response->body, "subaccount_number");
	sub.balance = extract_int(response->body, "balance");
	return sub;
}

Result<SubaccountTransfer> KalshiClient::transfer_subaccount(const SubaccountTransfer& request) {
	if (request.client_transfer_id.empty() || request.amount_cents <= 0) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest,
				  "subaccount transfer requires client_transfer_id and positive amount_cents"});
	}
	ser::SubaccountTransferBody body;
	body.client_transfer_id = request.client_transfer_id;
	body.from_subaccount = request.from_subaccount;
	body.to_subaccount = request.to_subaccount;
	body.amount_cents = request.amount_cents;
	body.exchange_index = request.exchange_index;
	Result<HttpResponse> response =
		impl_->transport->post("/portfolio/subaccounts/transfer", render_body(body));
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to transfer between subaccounts: " + response->body,
									 response->status_code});
	}
	SubaccountTransfer t;
	t.client_transfer_id = request.client_transfer_id;
	t.from_subaccount = request.from_subaccount;
	t.to_subaccount = request.to_subaccount;
	t.amount_cents = request.amount_cents;
	t.amount = request.amount_cents;
	t.exchange_index = request.exchange_index;
	return t;
}

Result<SubaccountBalances> KalshiClient::get_subaccount_balances() {
	Result<HttpResponse> response = impl_->transport->get("/portfolio/subaccounts/balances");
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get subaccount balances: " + std::to_string(response->status_code),
				  response->status_code});
	}
	SubaccountBalances result;
	for (const std::string& obj : extract_array_objects(response->body, "subaccount_balances")) {
		Subaccount sub;
		sub.subaccount_number = extract_int(obj, "subaccount_number");
		sub.balance_dollars = extract_string(obj, "balance");
		sub.balance = exact_scaled_int_or_zero(sub.balance_dollars, 2);
		sub.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		sub.updated_ts = extract_int(obj, "updated_ts");
		result.balances.push_back(sub);
	}
	return result;
}

Result<SubaccountTransfers>
KalshiClient::get_subaccount_transfers(const GetSubaccountTransfersParams& params) {
	std::string query = "/portfolio/subaccounts/transfers";
	if (params.limit) {
		append_query_param(query, "limit", *params.limit);
	}
	if (params.cursor) {
		append_query_param(query, "cursor", *params.cursor);
	}
	Result<HttpResponse> response = impl_->transport->get(query);
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get subaccount transfers: " + std::to_string(response->status_code),
				  response->status_code});
	}
	SubaccountTransfers result;
	for (const std::string& obj : extract_array_objects(response->body, "transfers")) {
		SubaccountTransfer t;
		t.transfer_id = extract_string(obj, "transfer_id");
		t.from_subaccount = extract_int(obj, "from_subaccount");
		t.to_subaccount = extract_int(obj, "to_subaccount");
		t.amount_cents = extract_int(obj, "amount_cents");
		t.amount = t.amount_cents;
		t.created_ts = extract_int(obj, "created_ts");
		t.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		result.transfers.push_back(t);
	}
	result.cursor = extract_string(response->body, "cursor");
	return result;
}

Result<void> KalshiClient::update_subaccount_netting(std::int64_t subaccount,
													 bool netting_enabled) {
	ser::SubaccountNettingBody body;
	body.subaccount_number = subaccount;
	body.enabled = netting_enabled;
	Result<HttpResponse> response =
		impl_->transport->put("/portfolio/subaccounts/netting", render_body(body));
	if (!response) {
		return std::unexpected(response.error());
	}
	// Kalshi documents the success response as 204/200 with no body —
	// accept either as the success path.
	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(Error{ErrorCode::ServerError,
									 "Failed to update subaccount netting: " + response->body,
									 response->status_code});
	}
	return {};
}

Result<SubaccountNettingList> KalshiClient::get_subaccount_netting() {
	Result<HttpResponse> response = impl_->transport->get("/portfolio/subaccounts/netting");
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get subaccount netting: " + std::to_string(response->status_code),
				  response->status_code});
	}
	SubaccountNettingList result;
	for (const std::string& obj : extract_array_objects(response->body, "netting_configs")) {
		SubaccountNetting n;
		n.subaccount = extract_int(obj, "subaccount_number");
		n.netting_enabled = extract_bool(obj, "enabled");
		n.exchange_index = static_cast<std::int32_t>(extract_int(obj, "exchange_index"));
		result.netting_settings.push_back(n);
	}
	return result;
}

Result<UserDataTimestamp> KalshiClient::get_user_data_timestamp() {
	Result<HttpResponse> response = impl_->transport->get("/exchange/user_data_timestamp");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to get user data timestamp: " + std::to_string(response->status_code),
				  response->status_code});
	}

	UserDataTimestamp result;
	result.as_of_time = extract_string(response->body, "as_of_time");
	return result;
}

Result<void> KalshiClient::delete_rfq(const std::string& rfq_id) {
	Result<HttpResponse> response = impl_->transport->del("/communications/rfqs/" + rfq_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to delete RFQ: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return {};
}

Result<void> KalshiClient::confirm_quote(const std::string& quote_id) {
	Result<HttpResponse> response =
		impl_->transport->put("/communications/quotes/" + quote_id + "/confirm", "{}");
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to confirm quote: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return {};
}

Result<void> KalshiClient::confirm_quote(const std::string& rfq_id,
										 const std::string& quote_id) {
	Result<HttpResponse> response = impl_->transport->put(
		"/communications/rfqs/" + rfq_id + "/quotes/" + quote_id + "/confirm", "{}");
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to confirm quote: " + std::to_string(response->status_code),
				  response->status_code});
	}
	return {};
}

Result<void> KalshiClient::delete_quote(const std::string& quote_id) {
	Result<HttpResponse> response = impl_->transport->del("/communications/quotes/" + quote_id);
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to delete quote: " + std::to_string(response->status_code),
				  response->status_code});
	}

	return {};
}

Result<void> KalshiClient::delete_quote(const std::string& rfq_id,
										const std::string& quote_id) {
	Result<HttpResponse> response =
		impl_->transport->del("/communications/rfqs/" + rfq_id + "/quotes/" + quote_id);
	if (!response) {
		return std::unexpected(response.error());
	}
	if (response->status_code != 200 && response->status_code != 204) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to delete quote: " + std::to_string(response->status_code),
				  response->status_code});
	}
	return {};
}

Result<ApiKey> KalshiClient::generate_api_key(const GenerateApiKeyParams& params) {
	if (params.name.empty() || params.expires_at ||
		(params.subaccount && params.fcm_subtrader_id)) {
		return std::unexpected(Error{ErrorCode::InvalidRequest,
									 "generated key request does not satisfy the current schema"});
	}
	// API requires stable key order; scopes field is omitted (NOT empty
	// array) when the caller passes no scopes — preserved here by
	// leaving `scopes = nullopt` when the input vector is empty.
	ser::GenerateApiKeyBody body;
	body.name = params.name;
	if (!params.scopes.empty()) {
		body.scopes = params.scopes;
	}
	body.subaccount = params.subaccount;
	body.fcm_subtrader_id = params.fcm_subtrader_id;

	Result<HttpResponse> response = impl_->transport->post("/api_keys/generate", render_body(body));
	if (!response) {
		return std::unexpected(response.error());
	}

	if (response->status_code != 200 && response->status_code != 201) {
		return std::unexpected(
			Error{ErrorCode::ServerError,
				  "Failed to generate API key: " + std::to_string(response->status_code),
				  response->status_code});
	}

	ApiKey key;
	// Parse from nested "api_key" object if present, otherwise from root
	std::string key_json = response->body;
	if (response->body.find("\"api_key\"") != std::string::npos) {
		// Extract the api_key object
		auto start = response->body.find("\"api_key\"");
		if (start != std::string::npos) {
			start = response->body.find('{', start);
			if (start != std::string::npos) {
				int depth = 1;
				size_t end = start + 1;
				while (end < response->body.size() && depth > 0) {
					if (response->body[end] == '{')
						depth++;
					else if (response->body[end] == '}')
						depth--;
					end++;
				}
				key_json = response->body.substr(start, end - start);
			}
		}
	}

	key.id = extract_string(key_json, "api_key_id");
	key.name = extract_string(key_json, "name");
	key.private_key = extract_string(key_json, "private_key");
	key.warning = extract_string(key_json, "warning");
	key.created_time = extract_int(key_json, "created_time");

	// Parse scopes array
	key.scopes = extract_string_array(key_json, "scopes");

	// Handle expires_at if present
	std::int64_t expires = extract_int(key_json, "expires_at");
	if (expires > 0) {
		key.expires_at = expires;
	}

	return key;
}

Result<LookupBundleResponse>
KalshiClient::lookup_multivariate_bundle(const std::string& collection_ticker,
										 const LookupBundleParams& params) {
	(void)collection_ticker;
	(void)params;
	return std::unexpected(
		Error{ErrorCode::InvalidRequest, "multivariate bundle lookup was removed upstream"});
}

} // namespace kalshi
