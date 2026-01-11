/// @file basic_usage.cpp
/// @brief Basic usage example for the Kalshi C++ SDK

#include <cstdlib>
#include <iostream>
#include <kalshi/kalshi.hpp>

int main() {
    // Get API credentials from environment
    const char* api_key_id = std::getenv("KALSHI_API_KEY_ID");
    const char* api_key_file = std::getenv("KALSHI_API_KEY_FILE");

    if (!api_key_id || !api_key_file) {
        std::cerr << "Please set KALSHI_API_KEY_ID and KALSHI_API_KEY_FILE environment variables\n";
        return 1;
    }

    // Create signer from PEM file
    kalshi::Result<kalshi::Signer> signer_result = kalshi::Signer::from_pem_file(api_key_id, api_key_file);
    if (!signer_result) {
        std::cerr << "Failed to create signer: " << signer_result.error().message << "\n";
        return 1;
    }

    // Create HTTP client
    kalshi::HttpClient client(std::move(*signer_result));

    // Make a test request
    kalshi::Result<kalshi::HttpResponse> response = client.get("/exchange/status");
    if (!response) {
        std::cerr << "Request failed: " << response.error().message << "\n";
        return 1;
    }

    std::cout << "Status: " << response->status_code << "\n";
    std::cout << "Response: " << response->body << "\n";

    return 0;
}
