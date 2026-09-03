#pragma once

#include "kalshi/error.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace kalshi {

/// Lossless fixed-point decimal received from or sent to Kalshi.
class FixedPoint {
public:
	[[nodiscard]] static Result<FixedPoint> parse(std::string_view value) {
		if (value.empty()) {
			return std::unexpected(Error{ErrorCode::InvalidRequest, "fixed-point value is empty"});
		}

		std::size_t index = value.front() == '-' ? 1 : 0;
		if (index == value.size()) {
			return std::unexpected(
				Error{ErrorCode::InvalidRequest, "fixed-point value has no digits"});
		}

		bool saw_digit = false;
		bool saw_decimal = false;
		bool fractional_digit = false;
		for (; index < value.size(); ++index) {
			const char ch = value[index];
			if (ch >= '0' && ch <= '9') {
				saw_digit = true;
				fractional_digit = fractional_digit || saw_decimal;
				continue;
			}
			if (ch == '.' && !saw_decimal && saw_digit) {
				saw_decimal = true;
				continue;
			}
			return std::unexpected(Error{ErrorCode::InvalidRequest, "invalid fixed-point value"});
		}
		if (!saw_digit || (saw_decimal && !fractional_digit)) {
			return std::unexpected(Error{ErrorCode::InvalidRequest, "invalid fixed-point value"});
		}
		return FixedPoint(std::string(value));
	}

	[[nodiscard]] const std::string& wire() const noexcept { return wire_; }

	/// Convert exactly to an integer with `scale` decimal places.
	/// Returns InvalidRequest instead of rounding or saturating.
	[[nodiscard]] Result<std::int64_t> scaled_integer(std::uint8_t scale) const {
		if (scale > 18) {
			return std::unexpected(
				Error{ErrorCode::InvalidRequest, "fixed-point scale exceeds int64 capacity"});
		}

		const bool negative = wire_.front() == '-';
		const std::size_t first = negative ? 1 : 0;
		const std::size_t decimal = wire_.find('.', first);
		const std::size_t whole_end = decimal == std::string::npos ? wire_.size() : decimal;
		const std::size_t fractional_start =
			decimal == std::string::npos ? wire_.size() : decimal + 1;
		const std::size_t fractional_size = wire_.size() - fractional_start;

		if (fractional_size > scale) {
			for (std::size_t i = fractional_start + scale; i < wire_.size(); ++i) {
				if (wire_[i] != '0') {
					return std::unexpected(Error{ErrorCode::InvalidRequest,
												 "fixed-point conversion would lose precision"});
				}
			}
		}

		const std::uint64_t positive_limit =
			negative ? std::uint64_t{std::numeric_limits<std::int64_t>::max()} + 1U
					 : std::uint64_t{std::numeric_limits<std::int64_t>::max()};
		std::uint64_t magnitude = 0;
		const auto append_digit = [&](char ch) -> bool {
			const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
			if (magnitude > (positive_limit - digit) / 10U) {
				return false;
			}
			magnitude = magnitude * 10U + digit;
			return true;
		};

		for (std::size_t i = first; i < whole_end; ++i) {
			if (!append_digit(wire_[i])) {
				return overflow_error();
			}
		}
		for (std::uint8_t i = 0; i < scale; ++i) {
			const char digit = i < fractional_size ? wire_[fractional_start + i] : '0';
			if (!append_digit(digit)) {
				return overflow_error();
			}
		}

		if (negative && magnitude == positive_limit) {
			return std::numeric_limits<std::int64_t>::min();
		}
		const std::int64_t signed_value = static_cast<std::int64_t>(magnitude);
		return negative ? -signed_value : signed_value;
	}

private:
	explicit FixedPoint(std::string wire) : wire_(std::move(wire)) {}

	[[nodiscard]] static Result<std::int64_t> overflow_error() {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "fixed-point value does not fit in int64"});
	}

	std::string wire_;
};

} // namespace kalshi
