#pragma once

#include "kalshi/error.hpp"
#include "kalshi/fixed_point.hpp"
#include "kalshi/models.hpp"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace kalshi {

/// Converts a dollar string such as "0.5600" to whole cents. Fails instead of
/// rounding when the price has sub-cent digits, as some markets do.
[[nodiscard]] inline Result<std::int64_t> to_cents(std::string_view dollars) {
	Result<FixedPoint> value = FixedPoint::parse(dollars);
	if (!value) {
		return std::unexpected(std::move(value.error()));
	}
	return value->scaled_integer(2);
}

/// Converts a fixed-point count such as "10.00" to whole contracts. Fails
/// for fractional counts.
[[nodiscard]] inline Result<std::int64_t> to_contracts(std::string_view count) {
	Result<FixedPoint> value = FixedPoint::parse(count);
	if (!value) {
		return std::unexpected(std::move(value.error()));
	}
	return value->scaled_integer(0);
}

using Timestamp = std::chrono::sys_time<std::chrono::milliseconds>;

/// Parses the RFC 3339 timestamps Kalshi returns, such as
/// "2026-09-25T14:00:00.123Z" or "2026-09-25T10:00:00-04:00".
[[nodiscard]] Result<Timestamp> parse_timestamp(std::string_view text);

/// The outcome a (side, action) pair is exposed to: buying yes or selling no
/// is yes exposure. Unknown if either input is Unknown.
[[nodiscard]] constexpr OutcomeSide outcome_side(Side side, Action action) noexcept {
	if (side == Side::Unknown || action == Action::Unknown) {
		return OutcomeSide::Unknown;
	}
	const bool yes = (side == Side::Yes) == (action == Action::Buy);
	return yes ? OutcomeSide::Yes : OutcomeSide::No;
}

/// The single-book side for a (side, action) pair: yes exposure bids.
/// Unknown if either input is Unknown.
[[nodiscard]] constexpr BookSide book_side(Side side, Action action) noexcept {
	switch (outcome_side(side, action)) {
		case OutcomeSide::Yes:
			return BookSide::Bid;
		case OutcomeSide::No:
			return BookSide::Ask;
		case OutcomeSide::Unknown:
			break;
	}
	return BookSide::Unknown;
}

} // namespace kalshi
