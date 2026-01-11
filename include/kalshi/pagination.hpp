#pragma once

#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kalshi {

/// Cursor for pagination
struct Cursor {
    std::string value;

    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
};

/// Pagination parameters for list requests
struct PaginationParams {
    std::optional<std::int32_t> limit;
    std::optional<Cursor> cursor;
};

/// Paginated response containing items and cursor for next page
template <typename T>
struct PaginatedResponse {
    std::vector<T> items;
    std::optional<Cursor> next_cursor;

    [[nodiscard]] bool has_more() const noexcept {
        return next_cursor.has_value() && !next_cursor->empty();
    }
};

/// Iterator for paginated results
///
/// Automatically fetches next pages as needed.
template <typename T>
class PaginatedIterator {
public:
    using FetchFunction = std::function<Result<PaginatedResponse<T>>(const PaginationParams&)>;

    PaginatedIterator(FetchFunction fetch, std::int32_t page_size = 100)
        : fetch_(std::move(fetch)), page_size_(page_size) {}

    /// Fetch the next page of results
    [[nodiscard]] Result<std::vector<T>> next_page() {
        PaginationParams params;
        params.limit = page_size_;
        if (current_cursor_) {
            params.cursor = current_cursor_;
        }

        Result<PaginatedResponse<T>> result = fetch_(params);
        if (!result) {
            return std::unexpected(result.error());
        }

        current_cursor_ = result->next_cursor;
        has_more_ = result->has_more();
        return std::move(result->items);
    }

    /// Check if there are more pages
    [[nodiscard]] bool has_more() const noexcept { return has_more_; }

    /// Fetch all remaining results (use with caution for large datasets)
    [[nodiscard]] Result<std::vector<T>> fetch_all() {
        std::vector<T> all_items;

        do {
            Result<std::vector<T>> page = next_page();
            if (!page) {
                return std::unexpected(page.error());
            }
            all_items.insert(all_items.end(), std::make_move_iterator(page->begin()),
                             std::make_move_iterator(page->end()));
        } while (has_more_);

        return all_items;
    }

private:
    FetchFunction fetch_;
    std::int32_t page_size_;
    std::optional<Cursor> current_cursor_;
    bool has_more_{true};
};

/// Build query string with pagination parameters
[[nodiscard]] inline std::string build_paginated_query(std::string_view base_path,
                                                       const PaginationParams& params) {
    std::string query{base_path};
    bool has_params = (base_path.find('?') != std::string_view::npos);

    if (params.limit) {
        query += has_params ? "&" : "?";
        query += "limit=" + std::to_string(*params.limit);
        has_params = true;
    }

    if (params.cursor && !params.cursor->empty()) {
        query += has_params ? "&" : "?";
        query += "cursor=" + params.cursor->value;
    }

    return query;
}

} // namespace kalshi
