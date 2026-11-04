#pragma once

#include "kalshi/fixed_point.hpp"
#include "kalshi/ws_models.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace example {

// Prices in ten-thousandths of a dollar, counts in hundredths of a contract.
using Levels = std::map<std::int64_t, std::int64_t>;
struct Book {
	Levels yes;
	Levels no;
};

inline std::optional<std::int64_t> scaled(const std::string& text, std::uint8_t scale) {
	const kalshi::Result<kalshi::FixedPoint> value = kalshi::FixedPoint::parse(text);
	if (!value) {
		return std::nullopt;
	}
	const kalshi::Result<std::int64_t> integer = value->scaled_integer(scale);
	return integer ? std::optional<std::int64_t>{*integer} : std::nullopt;
}

// Build a complete side before replacing it; malformed numbers never become zero.
inline bool load(Levels& levels,
				 const std::optional<std::vector<std::array<std::string, 2>>>& snapshot) {
	Levels replacement;
	if (snapshot) {
		for (const std::array<std::string, 2>& level : *snapshot) {
			const std::optional<std::int64_t> price = scaled(level[0], 4);
			const std::optional<std::int64_t> count = scaled(level[1], 2);
			if (!price || *price < 0 || *price > 10000 || !count || *count < 0) {
				return false;
			}
			if (*count > 0) {
				replacement[*price] = *count;
			}
		}
	}
	levels = std::move(replacement);
	return true;
}

inline bool apply_delta(Levels& levels, const std::string& price_text,
						const std::string& delta_text) {
	const std::optional<std::int64_t> price = scaled(price_text, 4);
	const std::optional<std::int64_t> delta = scaled(delta_text, 2);
	if (!price || *price < 0 || *price > 10000 || !delta) {
		return false;
	}
	const Levels::const_iterator found = levels.find(*price);
	const std::int64_t previous = found == levels.end() ? 0 : found->second;
	if ((*delta > 0 && previous > (std::numeric_limits<std::int64_t>::max)() - *delta) ||
		(*delta < 0 && previous < (std::numeric_limits<std::int64_t>::min)() - *delta)) {
		return false;
	}
	const std::int64_t count = previous + *delta;
	if (count <= 0) {
		levels.erase(*price);
	} else {
		levels[*price] = count;
	}
	return true;
}

// A missing book ignores deltas until a valid snapshot restores it.
class Orderbooks {
public:
	struct Change {
		// The book is borrowed until the next update.
		std::string ticker;
		const Book* book{nullptr};
		bool needs_snapshot{false};
	};

	Change consume(const kalshi::ws::Message& message) {
		using Snapshot = kalshi::ws::Update<kalshi::ws::OrderbookSnapshot>;
		using Delta = kalshi::ws::Update<kalshi::ws::OrderbookDelta>;
		if (const Snapshot* snapshot = std::get_if<Snapshot>(&message)) {
			const std::string& ticker = snapshot->msg.market_ticker;
			Book replacement;
			if (!load(replacement.yes, snapshot->msg.yes_dollars_fp) ||
				!load(replacement.no, snapshot->msg.no_dollars_fp)) {
				books_.erase(ticker);
				return {ticker, nullptr, true};
			}
			const std::map<std::string, Book>::iterator found =
				books_.insert_or_assign(ticker, std::move(replacement)).first;
			return {ticker, &found->second, false};
		}
		if (const Delta* delta = std::get_if<Delta>(&message)) {
			const std::string& ticker = delta->msg.market_ticker;
			const std::map<std::string, Book>::iterator found = books_.find(ticker);
			if (found == books_.end())
				return {};
			Book& book = found->second;
			Levels& side = delta->msg.side == kalshi::Side::Yes ? book.yes : book.no;
			if (delta->msg.side == kalshi::Side::Unknown ||
				!apply_delta(side, delta->msg.price_dollars, delta->msg.delta_fp)) {
				books_.erase(found);
				return {ticker, nullptr, true};
			}
			return {ticker, &book, false};
		}
		return {};
	}

private:
	std::map<std::string, Book> books_;
};

} // namespace example
