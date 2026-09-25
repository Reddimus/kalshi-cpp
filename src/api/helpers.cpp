#include "kalshi/helpers.hpp"

#include <charconv>

namespace kalshi {

namespace {

bool read_digits(std::string_view text, std::size_t pos, std::size_t count, int& out) {
	if (pos + count > text.size()) {
		return false;
	}
	const char* first = text.data() + pos;
	const std::from_chars_result parsed = std::from_chars(first, first + count, out);
	return parsed.ec == std::errc{} && parsed.ptr == first + count;
}

Result<Timestamp> invalid(std::string_view text) {
	return std::unexpected(Error{
		ErrorCode::ParseError, "Invalid RFC 3339 timestamp: '" + std::string(text) + "'", 0, {}});
}

} // namespace

Result<Timestamp> parse_timestamp(std::string_view text) {
	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	if (text.size() < 20 || text[4] != '-' || text[7] != '-' ||
		(text[10] != 'T' && text[10] != 't') || text[13] != ':' || text[16] != ':' ||
		!read_digits(text, 0, 4, year) || !read_digits(text, 5, 2, month) ||
		!read_digits(text, 8, 2, day) || !read_digits(text, 11, 2, hour) ||
		!read_digits(text, 14, 2, minute) || !read_digits(text, 17, 2, second) || hour > 23 ||
		minute > 59 || second > 60) {
		return invalid(text);
	}
	const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month(month),
										   std::chrono::day(day)};
	if (!date.ok()) {
		return invalid(text);
	}

	std::size_t pos = 19;
	std::int64_t millis = 0;
	if (text[pos] == '.') {
		++pos;
		const std::size_t digits_start = pos;
		while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
			if (pos - digits_start < 3) {
				millis = millis * 10 + (text[pos] - '0');
			}
			++pos;
		}
		const std::size_t digits = pos - digits_start;
		if (digits == 0) {
			return invalid(text);
		}
		for (std::size_t i = digits; i < 3; ++i) {
			millis *= 10;
		}
	}

	std::chrono::minutes offset{0};
	if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
		++pos;
	} else if (pos + 6 == text.size() && (text[pos] == '+' || text[pos] == '-') &&
			   text[pos + 3] == ':') {
		int offset_hours = 0;
		int offset_minutes = 0;
		if (!read_digits(text, pos + 1, 2, offset_hours) ||
			!read_digits(text, pos + 4, 2, offset_minutes) || offset_hours > 23 ||
			offset_minutes > 59) {
			return invalid(text);
		}
		offset = std::chrono::hours{offset_hours} + std::chrono::minutes{offset_minutes};
		if (text[pos] == '-') {
			offset = -offset;
		}
		pos += 6;
	} else {
		return invalid(text);
	}
	if (pos != text.size()) {
		return invalid(text);
	}

	const std::chrono::sys_days days{date};
	return Timestamp{days} + std::chrono::hours{hour} + std::chrono::minutes{minute} +
		   std::chrono::seconds{second} + std::chrono::milliseconds{millis} - offset;
}

} // namespace kalshi
