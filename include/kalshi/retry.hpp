#pragma once

#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <thread>

namespace kalshi {

/// Retry policy configuration
struct RetryPolicy {
    std::int32_t max_attempts{3};
    std::chrono::milliseconds initial_delay{100};
    std::chrono::milliseconds max_delay{10000};
    double backoff_multiplier{2.0};
    double jitter_factor{0.1}; // Random jitter as fraction of delay
    bool retry_on_network_error{true};
    bool retry_on_rate_limit{true};
    bool retry_on_server_error{true}; // 5xx errors
};

/// Result of a retry operation
struct RetryResult {
    std::int32_t attempts_made;
    std::chrono::milliseconds total_delay;
    bool succeeded;
};

/// Determines if a response should be retried
[[nodiscard]] inline bool should_retry(const HttpResponse& response,
                                       const RetryPolicy& policy) noexcept {
    // Rate limited (429)
    if (policy.retry_on_rate_limit && response.status_code == 429) {
        return true;
    }
    // Server errors (5xx)
    if (policy.retry_on_server_error && response.status_code >= 500) {
        return true;
    }
    return false;
}

/// Determines if an error should be retried
[[nodiscard]] inline bool should_retry(const Error& error, const RetryPolicy& policy) noexcept {
    if (policy.retry_on_network_error && error.code == ErrorCode::NetworkError) {
        return true;
    }
    return false;
}

/// Calculate delay for a retry attempt with exponential backoff and jitter
[[nodiscard]] inline std::chrono::milliseconds calculate_retry_delay(std::int32_t attempt,
                                                                     const RetryPolicy& policy) {
    // Exponential backoff
    double delay_ms = static_cast<double>(policy.initial_delay.count());
    for (std::int32_t i = 1; i < attempt; ++i) {
        delay_ms *= policy.backoff_multiplier;
    }

    // Cap at max delay
    delay_ms = std::min(delay_ms, static_cast<double>(policy.max_delay.count()));

    // Add jitter
    if (policy.jitter_factor > 0) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<double> dist(1.0 - policy.jitter_factor,
                                                    1.0 + policy.jitter_factor);
        delay_ms *= dist(rng);
    }

    return std::chrono::milliseconds{static_cast<std::int64_t>(delay_ms)};
}

/// Execute an HTTP request with retry logic
///
/// @param operation Function that performs the HTTP request
/// @param policy Retry configuration
/// @return Result of the operation after retries
template <typename Operation>
[[nodiscard]] Result<HttpResponse> with_retry(Operation&& operation, const RetryPolicy& policy) {
    std::chrono::milliseconds total_delay{0};

    for (std::int32_t attempt = 1; attempt <= policy.max_attempts; ++attempt) {
        Result<HttpResponse> result = operation();

        if (result.has_value()) {
            // Check if we should retry based on response
            if (should_retry(*result, policy) && attempt < policy.max_attempts) {
                std::chrono::milliseconds delay = calculate_retry_delay(attempt, policy);
                total_delay += delay;
                std::this_thread::sleep_for(delay);
                continue;
            }
            return result;
        }

        // Check if we should retry based on error
        if (should_retry(result.error(), policy) && attempt < policy.max_attempts) {
            std::chrono::milliseconds delay = calculate_retry_delay(attempt, policy);
            total_delay += delay;
            std::this_thread::sleep_for(delay);
            continue;
        }

        return result;
    }

    return std::unexpected(Error::network("Max retry attempts exceeded"));
}

/// HTTP client wrapper with automatic retries
class RetryingClient {
public:
    RetryingClient(HttpClient& client, RetryPolicy policy = {});

    /// Make a GET request with retries
    [[nodiscard]] Result<HttpResponse> get(std::string_view path);

    /// Make a POST request with retries
    [[nodiscard]] Result<HttpResponse> post(std::string_view path, std::string_view body = {});

    /// Make a PUT request with retries
    [[nodiscard]] Result<HttpResponse> put(std::string_view path, std::string_view body = {});

    /// Make a DELETE request with retries
    [[nodiscard]] Result<HttpResponse> del(std::string_view path);

    /// Get the retry policy
    [[nodiscard]] const RetryPolicy& policy() const noexcept;

    /// Set the retry policy
    void set_policy(RetryPolicy policy) noexcept;

private:
    HttpClient& client_;
    RetryPolicy policy_;
};

} // namespace kalshi
