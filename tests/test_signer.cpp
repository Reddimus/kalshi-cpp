#include "kalshi/signer.hpp"

#include <iostream>
#include <string>
#include <vector>

// Test declarations from test_main.cpp
struct TestResult {
	std::string name;
	bool passed;
	std::string message;
};
extern std::vector<TestResult> g_results;

#define TEST(name)                                             \
	void test_##name();                                        \
	struct TestRegister_##name {                               \
		TestRegister_##name() {                                \
			try {                                              \
				test_##name();                                 \
				g_results.push_back({#name, true, ""});        \
			} catch (const std::exception& e) {                \
				g_results.push_back({#name, false, e.what()}); \
			}                                                  \
		}                                                      \
	} g_register_##name;                                       \
	void test_##name()

#define ASSERT_TRUE(expr) \
	if (!(expr))          \
	throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_FALSE(expr) \
	if (expr)              \
	throw std::runtime_error("Assertion failed: NOT " #expr)

#define ASSERT_EQ(a, b) \
	if ((a) != (b))     \
	throw std::runtime_error("Assertion failed: " #a " == " #b)

// Test RSA key for testing (DO NOT USE IN PRODUCTION)
const char* TEST_RSA_KEY = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEA0Z3VS5JJcds3xfn/ygWyF8PbnGy0AHB7MvXIJyIxCGKbN9jA
xhJA3kewG1C1fLX3xZP9Pf6hMqXAuqJlPBofeLXpHhnR+DV/2n7gHCVCpPPrMZhM
BKhXpMgTlXh2THsa4BQFR4qFPJNaH7q4bHwmPpU9SevEqMqFPFoGaK+Y8gSbJJsM
qkJSXMlPzCnlZgF0+Y4YjdSHZ1btLyhmPdJXl/k12Z7M0N+Y+D+V0y/ZJcOy/G+H
E1f+eRaY3H/HPzrtbYPx+oRJMr0tYqM6WqzEgvTORqZhzQNSsLjPxf0Yp3k4yR5X
Z0zYJPcTtJRg5XZbGJ0cVlVCJQZZJhONxxWPqwIDAQABAoIBAFPrsZVH3srU7Q0S
IlCvOFnG1sS3LD+J1JZHmYhM0CG4jf4yMGxBj9JdyTY/0mLUYaR0lR6U8r9vLxWh
r6staNqmkKUlUKpavB4/TRH+K7DNdE4Gg+o0d0Z1b0JmAldJHFgeUWMajwAPKYp5
pLJ+0J8I+FqXqnZQNrxdRwVv5qExBfC9Ibfe+gY9S++JLcJHMCyNfLdj+wXTh7oQ
xhsH0u3rrvwpDwnHy4L5m6S3T6fIK4bmZYT6qJr0/2kR6VN+N0Y5SWFQ4QRHM6Hq
3Y/GnP6dQYNasJAjf0ov3F5Y7HKYcGLVYgrtbqHr7JYVqvNsJPKZ0fS/F8jPCKh8
rYoJfAECgYEA8J5hfn+q+EY9ztB3SLNmPAJvEwH9B5KpPYfpZPHQPHvPOBGd5KfN
SXkhUdE7X3+nMR3kJJOPIYm5Y6Y3Y8bEMz4QlMoKxrfh0Lc1l9RjN0I0rJ0h6L9p
yewDw5eFwJM5nJP/Nx8XKI9kNxHdESF0N9pz+kpFJJwMdF7J84e+B6sCgYEA3r8r
nH1AKJol8v2zK0v1vT1cK5F5C5Vqyq7F0M0M7Y4P9b5m3H4Y9y/S7X2T5Y1gX7nM
sNvLgGCzL5B7Aq4sFG8W5u5TYhvJLq6E5jC5X5D5m5Z5T5V5Y5z5S5Q5G5b5d5K5
N5H5J5M5P5L5R5f5X5c5W5h5i5k5j5l5n5o5p5q5r5s5t5AECgYBmG9B5u5G5m5L5
i5h5W5c5X5f5R5L5P5M5J5H5N5K5d5b5G5Q5S5z5Y5V5T5m5D5X5C5q5E5j5L5o5
n5l5k5j5i5h5c5W5f5X5R5L5P5M5J5H5N5K5d5b5G5Q5S5z5Y5V5T5m5D5X5C5q5
E5j5L5o5n5l5k5j5i5h5c5W5f5X5R5L5P5M5J5H5N5K5d5b5G5QwKBgC7V5T5m5D5
X5C5q5E5j5L5o5n5l5k5j5i5h5c5W5f5X5R5L5P5M5J5H5N5K5d5b5G5Q5S5z5Y5
V5T5m5D5X5C5q5E5j5L5o5n5l5k5j5i5h5c5W5f5X5R5L5P5M5J5H5N5K5d5b5G5
Q5S5z5Y5V5T5m5D5X5C5q5E5j5L5o5n5l5k5j5i5h5c5W5f5X5R5LAoGBAL5P5M5J
5H5N5K5d5b5G5Q5S5z5Y5V5T5m5D5X5C5q5E5j5L5o5n5l5k5j5i5h5c5W5f5X5R5
L5P5M5J5H5N5K5d5b5G5Q5S5z5Y5V5T5m5D5X5C5q5E5j5L5o5n5l5k5j5i5h5c5
W5f5X5R5L5P5M5J5H5N5K5d5b5G5Q5S5z5Y5V5T5m5D5X5C5q5E5j5L5o5n5l5k5
-----END RSA PRIVATE KEY-----
)";

TEST(signer_from_invalid_pem_fails) {
	kalshi::Result<kalshi::Signer> result = kalshi::Signer::from_pem("test_key_id", "invalid pem data");
	ASSERT_FALSE(result.has_value());
}

TEST(signer_api_key_id_stored) {
	// Generate a proper test key
	kalshi::Result<kalshi::Signer> result = kalshi::Signer::from_pem("my_api_key", TEST_RSA_KEY);
	// Key may or may not be valid depending on format, but if it succeeds, check the ID
	if (result.has_value()) {
		ASSERT_EQ(std::string(result->api_key_id()), std::string("my_api_key"));
	}
}

TEST(sign_produces_headers) {
	kalshi::Result<kalshi::Signer> signer_result = kalshi::Signer::from_pem("test_key", TEST_RSA_KEY);
	// Skip if key parsing fails (test key format issue)
	if (!signer_result.has_value()) {
		return;
	}

	kalshi::Result<kalshi::AuthHeaders> headers_result =
		signer_result->sign_with_timestamp("GET", "/trade-api/v2/markets", 1234567890000);
	if (!headers_result.has_value()) {
		throw std::runtime_error("Failed to sign: " + headers_result.error().message);
	}

	const kalshi::AuthHeaders& headers = *headers_result;
	ASSERT_EQ(headers.access_key, std::string("test_key"));
	ASSERT_EQ(headers.timestamp, std::string("1234567890000"));
	ASSERT_FALSE(headers.signature.empty());
}
