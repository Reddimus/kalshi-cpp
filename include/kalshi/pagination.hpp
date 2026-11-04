#pragma once

#include "kalshi/error.hpp"

#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kalshi {

namespace detail {

inline std::string take_page_cursor(std::string& cursor) {
	return std::move(cursor);
}

inline std::string take_page_cursor(std::optional<std::string>& cursor) {
	return std::move(cursor).value_or("");
}

} // namespace detail

/// Follows cursors through a list operation and collects every page's items.
///
/// `fetch` takes the cursor to request (empty for the first page) and returns
/// the response; `items` names the response member that holds each page.
/// Responses may name their continuation `cursor` or `next_cursor`.
/// Collection stops after `max_pages` pages even if more remain, and fails with
/// `ParseError` if the server repeats a continuation cursor.
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
			  std::size_t max_pages = (std::numeric_limits<std::size_t>::max)()) {
	std::vector<typename Items::value_type> all;
	std::string cursor;
	std::unordered_set<std::string> seen;
	for (std::size_t page = 0; page < max_pages; ++page) {
		Result<Response> response = fetch(std::string_view{cursor});
		if (!response) {
			return std::unexpected(std::move(response.error()));
		}
		Items& batch = (*response).*items;
		all.insert(all.end(), std::make_move_iterator(batch.begin()),
				   std::make_move_iterator(batch.end()));
		std::string next;
		if constexpr (requires { response->cursor; }) {
			next = detail::take_page_cursor(response->cursor);
		} else {
			next = detail::take_page_cursor(response->next_cursor);
		}
		if (next.empty()) {
			break;
		}
		if (!seen.insert(next).second) {
			return std::unexpected(
				Error{ErrorCode::ParseError, "Pagination cursor repeated: " + next, 0, {}});
		}
		cursor = std::move(next);
	}
	return all;
}

} // namespace kalshi
