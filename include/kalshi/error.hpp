#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace kalshi {

/// Error codes for Kalshi SDK operations
enum class ErrorCode {
    Ok = 0,
    NetworkError,
    AuthenticationError,
    InvalidRequest,
    RateLimited,
    ServerError,
    ParseError,
    SigningError,
    InvalidKey,
    Unknown
};

/// Error information returned by SDK operations
struct Error {
    ErrorCode code;
    std::string message;
    int http_status{0};

    [[nodiscard]] constexpr bool is_ok() const noexcept { return code == ErrorCode::Ok; }

    [[nodiscard]] static Error ok() { return {ErrorCode::Ok, ""}; }

    [[nodiscard]] static Error network(std::string msg) {
        return {ErrorCode::NetworkError, std::move(msg)};
    }

    [[nodiscard]] static Error auth(std::string msg) {
        return {ErrorCode::AuthenticationError, std::move(msg)};
    }

    [[nodiscard]] static Error parse(std::string msg) {
        return {ErrorCode::ParseError, std::move(msg)};
    }

    [[nodiscard]] static Error signing(std::string msg) {
        return {ErrorCode::SigningError, std::move(msg)};
    }
};

/// Result type for SDK operations
template <typename T>
using Result = std::expected<T, Error>;

} // namespace kalshi
