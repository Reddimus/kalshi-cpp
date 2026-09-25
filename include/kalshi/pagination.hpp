#pragma once

#include "kalshi/error.hpp"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kalshi {

/// Follows cursors through a list operation and collects every page's items.
///
/// `fetch` takes the cursor to request (empty for the first page) and returns
/// the response; `items` names the response member that holds each page.
/// Collection stops after `max_pages` pages even if more remain, and fails with
/// `ParseError` if the server returns the cursor it was just given.
///
///     kalshi::GetMarketsParams params{.limit = 1000, .series_ticker = "KXHIGHNY"};
///     kalshi::Result<std::vector<kalshi::Market>> markets = kalshi::collect_pages(
///         [&](std::string_view cursor) {
///             params.cursor = cursor.empty() ? std::nullopt : std::optional<std::string>(cursor);
///             return client.get_markets(params);
///         },
///         &kalshi::GetMarketsResponse::markets);
template <class Fetch, class Response, class Items>
[[nodiscard]] Result<std::vector<typename Items::value_type>>
collect_pages(Fetch&& fetch, Items Response::*items,
			  std::size_t max_pages = std::numeric_limits<std::size_t>::max()) {
	std::vector<typename Items::value_type> all;
	std::string cursor;
	for (std::size_t page = 0; page < max_pages; ++page) {
		Result<Response> response = fetch(std::string_view{cursor});
		if (!response) {
			return std::unexpected(std::move(response.error()));
		}
		Items& batch = (*response).*items;
		all.insert(all.end(), std::make_move_iterator(batch.begin()),
				   std::make_move_iterator(batch.end()));
		std::string next;
		if constexpr (std::is_same_v<decltype(response->cursor), std::optional<std::string>>) {
			next = response->cursor.value_or("");
		} else {
			next = std::move(response->cursor);
		}
		if (next.empty()) {
			break;
		}
		if (next == cursor) {
			return std::unexpected(
				Error{ErrorCode::ParseError, "Pagination cursor did not advance: " + next, 0, {}});
		}
		cursor = std::move(next);
	}
	return all;
}

} // namespace kalshi
