/// @file get_daily_high_temp.cpp
/// @brief Example: Get highest temperature prediction markets for cities today
///
/// This example demonstrates:
/// 1. Fetching series/events for daily high temperature markets
/// 2. Getting market details with current bid/ask prices
/// 3. Querying historical candlestick (OHLC) data for price history
///
/// Candlestick API Notes:
/// - Endpoint: GET /series/{series_ticker}/markets/{ticker}/candlesticks
/// - period_interval: 1 (1min), 60 (1hr), 1440 (1day) in MINUTES
/// - Historical data is available for markets that have trading activity
/// - Settled markets may still return historical data if within retention period

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <kalshi/kalshi.hpp>
#include <string>
#include <thread>
#include <vector>

// Helper to format unix timestamp
std::string format_timestamp(std::int64_t ts) {
	std::time_t time = static_cast<std::time_t>(ts);
	std::tm* tm = std::gmtime(&time);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
	return buf;
}

// Print candlestick preview (head or tail based on count)
void print_candle_preview(const std::vector<kalshi::Candlestick>& candles,
						  const std::string& series_ticker, const std::string& market_ticker,
						  size_t preview_count = 9) {
	if (candles.empty()) {
		std::cout << "      [" << series_ticker << "/" << market_ticker << "] "
				  << "No candlestick data available\n";
		return;
	}

	std::cout << "\n      ┌─────────────────────────────────────────────────────────────────┐\n";
	std::cout << "      │ CANDLESTICK DATA: " << series_ticker << " / " << market_ticker << "\n";
	std::cout << "      │ Total candles: " << candles.size() << " | Period: 1h | Range: 7 days\n";
	std::cout << "      ├─────────────────────────────────────────────────────────────────┤\n";

	// Calculate stats
	std::int32_t min_low = INT32_MAX, max_high = 0;
	std::int64_t total_volume = 0;
	for (const auto& c : candles) {
		if (c.low_price > 0 && c.low_price < min_low)
			min_low = c.low_price;
		if (c.high_price > max_high)
			max_high = c.high_price;
		total_volume += c.volume;
	}

	std::cout << "      │ Stats: Low=" << (min_low == INT32_MAX ? 0 : min_low)
			  << "c High=" << max_high << "c TotalVol=" << total_volume << "\n";
	std::cout << "      ├─────────────────────────────────────────────────────────────────┤\n";

	// Determine if we show head or tail (show tail for recent data)
	bool show_tail = candles.size() > preview_count;
	size_t start_idx = show_tail ? candles.size() - preview_count : 0;
	size_t end_idx = show_tail ? candles.size() : std::min(preview_count, candles.size());

	if (show_tail) {
		std::cout << "      │ ... (" << start_idx << " earlier candles omitted)\n";
		std::cout << "      │ Showing TAIL " << preview_count << " of " << candles.size()
				  << " candles:\n";
	} else {
		std::cout << "      │ Showing HEAD " << end_idx << " of " << candles.size()
				  << " candles:\n";
	}

	std::cout << "      │ ─────────────────────────────────────────────────────────────────\n";
	std::cout << "      │  #   Timestamp          Open  High   Low Close   Vol\n";
	std::cout << "      │ ─────────────────────────────────────────────────────────────────\n";

	for (size_t i = start_idx; i < end_idx; ++i) {
		const auto& c = candles[i];
		std::cout << "      │ " << std::setw(3) << (i + 1) << "  " << format_timestamp(c.timestamp)
				  << "   " << std::setw(4) << c.open_price << "  " << std::setw(4) << c.high_price
				  << "  " << std::setw(4) << c.low_price << "  " << std::setw(4) << c.close_price
				  << "  " << std::setw(5) << c.volume << "\n";
	}

	std::cout << "      └─────────────────────────────────────────────────────────────────┘\n";
}

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

	std::cout << "╔═══════════════════════════════════════════════════════════════════════╗\n";
	std::cout << "║       KALSHI DAILY HIGH TEMPERATURE MARKETS - HISTORICAL DATA        ║\n";
	std::cout << "╚═══════════════════════════════════════════════════════════════════════╝\n\n";

	// Known daily high temperature series tickers on Kalshi
	std::vector<std::pair<std::string, std::string>> temp_series = {
		{"KXHIGHLAX", "Los Angeles"},	 {"KXHIGHNY", "NYC"},
		{"KXHIGHAUS", "Austin"},		 {"KXHIGHDEN", "Denver"},
		{"KXHIGHOU", "Houston"},		 {"KXHIGHTLV", "Las Vegas"},
		{"KXHIGHTSFO", "San Francisco"}, {"KXHIGHTDC", "Washington DC"},
		{"KXHIGHPHIL", "Philadelphia"},	 {"HIGHMIA", "Miami"},
	};

	std::cout << "Scanning " << temp_series.size() << " temperature series for active markets...\n";
	std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

	int found_count = 0;
	int total_markets = 0;
	int total_candles = 0;

	// Current time for historical data queries
	auto now = std::chrono::system_clock::now();
	std::int64_t now_ts =
		std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
	std::int64_t seven_days_ago = now_ts - (7 * 24 * 60 * 60);

	for (const auto& [series_ticker, city_name] : temp_series) {
		// Rate limit to avoid 429 errors
		std::this_thread::sleep_for(std::chrono::milliseconds(150));

		// Get events for this temperature series
		kalshi::GetEventsParams event_params;
		event_params.series_ticker = series_ticker;
		event_params.status = "open";

		kalshi::Result<kalshi::PaginatedResponse<kalshi::Event>> events_result =
			client.get_events(event_params);
		if (!events_result) {
			std::cout << "  [" << series_ticker << "] " << city_name
					  << " - ERROR: " << events_result.error().message << "\n";
			continue;
		}

		const std::vector<kalshi::Event>& events = events_result->items;
		if (events.empty()) {
			std::cout << "  [" << series_ticker << "] " << city_name << " - No active events\n";
			continue;
		}

		for (const kalshi::Event& event : events) {
			std::cout
				<< "\n┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n";
			std::cout << "┃ " << event.title << "\n";
			std::cout
				<< "┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n";
			std::cout << "┃ Series: " << series_ticker << " (" << city_name << ")\n";
			std::cout << "┃ Event:  " << event.event_ticker << "\n";
			if (!event.sub_title.empty()) {
				std::cout << "┃ Date:   " << event.sub_title << "\n";
			}
			std::cout
				<< "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n";

			// Rate limit between market requests
			std::this_thread::sleep_for(std::chrono::milliseconds(150));

			// Get markets for this event
			kalshi::GetMarketsParams market_params;
			market_params.event_ticker = event.event_ticker;

			kalshi::Result<kalshi::PaginatedResponse<kalshi::Market>> markets_result =
				client.get_markets(market_params);
			if (!markets_result) {
				std::cout << "  ✗ Failed to get markets: " << markets_result.error().message
						  << "\n\n";
				continue;
			}

			const std::vector<kalshi::Market>& markets = markets_result->items;
			if (markets.empty()) {
				std::cout << "  ✗ No markets available\n\n";
				continue;
			}

			total_markets += static_cast<int>(markets.size());
			std::cout << "\n  Markets (" << markets.size() << " temperature brackets):\n";
			std::cout << "  ────────────────────────────────────────────────────────────────\n";

			for (const kalshi::Market& market : markets) {
				std::cout << "    • [" << market.ticker << "] " << market.title;
				if (market.yes_bid > 0 || market.yes_ask > 0) {
					std::cout << "\n      Bid: " << market.yes_bid << "c | Ask: " << market.yes_ask
							  << "c";
				}
				std::cout << "\n";

				// Fetch historical candlestick data for each market
				std::this_thread::sleep_for(std::chrono::milliseconds(150));

				kalshi::GetCandlesticksParams candle_params;
				candle_params.event_ticker = event.event_ticker; // Use event ticker, not series
				candle_params.ticker = market.ticker;
				candle_params.period_interval = 60; // 60 minutes = 1 hour candles
				candle_params.start_ts = seven_days_ago;
				candle_params.end_ts = now_ts;

				auto candles_result = client.get_market_candlesticks(candle_params);
				if (candles_result) {
					if (candles_result->empty()) {
						std::cout << "      [" << event.event_ticker << "/" << market.ticker
								  << "] API returned empty array (no trading data)\n";
					} else {
						total_candles += static_cast<int>(candles_result->size());
						print_candle_preview(
							*candles_result, event.event_ticker, market.ticker, 9);
					}
				} else {
					std::cout << "      [" << event.event_ticker << "/" << market.ticker << "] "
							  << "API ERROR: " << candles_result.error().message << " (http: "
							  << candles_result.error().http_status << ")\n";
				}
				std::cout << "\n";
			}
			++found_count;
		}
	}

	// Summary
	std::cout << "\n╔═══════════════════════════════════════════════════════════════════════╗\n";
	std::cout << "║                              SUMMARY                                  ║\n";
	std::cout << "╠═══════════════════════════════════════════════════════════════════════╣\n";
	std::cout << "║  Active Events:     " << std::setw(6) << found_count
			  << "                                        ║\n";
	std::cout << "║  Total Markets:     " << std::setw(6) << total_markets
			  << "                                        ║\n";
	std::cout << "║  Candles Fetched:   " << std::setw(6) << total_candles
			  << "                                        ║\n";
	std::cout << "╚═══════════════════════════════════════════════════════════════════════╝\n";

	if (found_count == 0) {
		std::cout << "\nNote: Daily temperature markets may not be available every day.\n";
		std::cout << "Available cities when markets are active:\n";
		std::cout << "  - Los Angeles, NYC, Austin, Denver, Houston, Las Vegas\n";
		std::cout << "  - San Francisco, Washington DC, Philadelphia, Miami\n";
	}

	// Print API usage notes
	std::cout << "\n┌─────────────────────────────────────────────────────────────────────┐\n";
	std::cout << "│                    HISTORICAL DATA API NOTES                        │\n";
	std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
	std::cout << "│ Endpoint: GET /series/{event}/markets/{ticker}/candlesticks        │\n";
	std::cout << "│ Periods:  1 (1min) | 60 (1hour) | 1440 (1day) - in MINUTES         │\n";
	std::cout << "│ Limits:   ~10 req/sec; paginate via start_ts/end_ts for backfills  │\n";
	std::cout << "└─────────────────────────────────────────────────────────────────────┘\n";

	return 0;
}
