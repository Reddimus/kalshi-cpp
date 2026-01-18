/// @file get_daily_high_temp.cpp
/// @brief Example: Get highest temperature prediction markets for cities today

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <kalshi/kalshi.hpp>
#include <string>
#include <thread>
#include <vector>

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

	// Create HTTP client and API client
	kalshi::HttpClient http_client(std::move(*signer_result));
	kalshi::KalshiClient client(std::move(http_client));

	std::cout << "Fetching daily high temperature prediction markets...\n\n";

	// Known daily high temperature series tickers on Kalshi
	// These represent "Highest temperature in X today?" markets
	std::vector<std::string> temp_series = {
		"KXHIGHLAX",   // Los Angeles
		"KXHIGHNY",    // NYC
		"KXHIGHAUS",   // Austin
		"KXHIGHDEN",   // Denver
		"KXHIGHOU",    // Houston
		"KXHIGHTLV",   // Las Vegas
		"KXHIGHTSFO",  // San Francisco
		"KXHIGHTDC",   // Washington DC
		"KXHIGHPHIL",  // Philadelphia
		"HIGHMIA",     // Miami
	};

	std::cout << "Checking " << temp_series.size() << " daily high temperature series...\n\n";

	int found_count = 0;

	for (const std::string& series_ticker : temp_series) {
		// Rate limit to avoid 429 errors
		std::this_thread::sleep_for(std::chrono::milliseconds(150));

		// Get events for this temperature series
		kalshi::GetEventsParams event_params;
		event_params.series_ticker = series_ticker;
		event_params.status = "open";

		kalshi::Result<kalshi::PaginatedResponse<kalshi::Event>> events_result =
			client.get_events(event_params);
		if (!events_result) {
			std::cerr << "Failed to get events for " << series_ticker << ": "
					  << events_result.error().message << "\n";
			continue;
		}

		const std::vector<kalshi::Event>& events = events_result->items;
		if (events.empty()) {
			continue;  // No active events for this series today
		}

		for (const kalshi::Event& event : events) {
			std::cout << "=== " << event.title << " ===\n";
			std::cout << "Series: " << series_ticker << "\n";
			std::cout << "Event: " << event.event_ticker << "\n";
			if (!event.sub_title.empty()) {
				std::cout << "Date: " << event.sub_title << "\n";
			}

			// Rate limit between market requests
			std::this_thread::sleep_for(std::chrono::milliseconds(150));

			// Get markets for this event
			kalshi::GetMarketsParams market_params;
			market_params.event_ticker = event.event_ticker;

			kalshi::Result<kalshi::PaginatedResponse<kalshi::Market>> markets_result =
				client.get_markets(market_params);
			if (!markets_result) {
				std::cerr << "  Failed to get markets: " << markets_result.error().message << "\n\n";
				continue;
			}

			const std::vector<kalshi::Market>& markets = markets_result->items;
			if (markets.empty()) {
				std::cout << "  No markets available\n\n";
				continue;
			}

			std::cout << "  Temperature ranges (" << markets.size() << " brackets):\n";
			for (const kalshi::Market& market : markets) {
				std::cout << "    [" << market.ticker << "] " << market.title;
				if (market.yes_bid > 0 || market.yes_ask > 0) {
					std::cout << " - Bid: " << market.yes_bid << "c, Ask: " << market.yes_ask << "c";
				}
				std::cout << "\n";
			}
			std::cout << "\n";
			++found_count;
		}
	}

	if (found_count == 0) {
		std::cout << "No daily high temperature markets are currently active.\n";
		std::cout << "\nNote: Daily temperature markets may not be available every day.\n";
		std::cout << "Available cities when markets are active:\n";
		std::cout << "  - Los Angeles, NYC, Austin, Denver, Houston, Las Vegas\n";
		std::cout << "  - San Francisco, Washington DC, Philadelphia, Miami\n";
	} else {
		std::cout << "Found " << found_count << " active daily high temperature event(s).\n";
	}

	return 0;
}
