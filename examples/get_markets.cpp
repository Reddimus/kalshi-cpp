/// @file get_markets.cpp
/// @brief Example: Get markets from Kalshi API

#include <cstdlib>
#include <iostream>
#include <kalshi/kalshi.hpp>

int main() {
	// Get API credentials from environment
	const char* api_key_id = std::getenv("KALSHI_API_KEY_ID");
	const char* api_key_file = std::getenv("KALSHI_API_KEY_FILE");

	if (!api_key_id || !api_key_file) {
		std::cerr << "Please set KALSHI_API_KEY_ID and KALSHI_API_KEY_FILE environment variables\n";
		std::cerr << "\nTo get API keys:\n";
		std::cerr << "  1. Go to https://kalshi.com/settings/api\n";
		std::cerr << "  2. Generate an API key pair\n";
		std::cerr << "  3. Download the private key PEM file\n";
		return 1;
	}

	// Create signer from PEM file
	kalshi::Result<kalshi::Signer> signer_result =
		kalshi::Signer::from_pem_file(api_key_id, api_key_file);
	if (!signer_result) {
		std::cerr << "Failed to create signer: " << signer_result.error().message << "\n";
		return 1;
	}

	// Create HTTP client
	kalshi::HttpClient client(std::move(*signer_result));

	// Get markets
	std::cout << "Fetching markets...\n\n";

	kalshi::Result<kalshi::HttpResponse> response = client.get("/markets?limit=10");
	if (!response) {
		std::cerr << "Request failed: " << response.error().message << "\n";
		return 1;
	}

	if (response->status_code != 200) {
		std::cerr << "API error (HTTP " << response->status_code << "): " << response->body << "\n";
		return 1;
	}

	std::cout << "Markets response:\n" << response->body << "\n";

	return 0;
}
