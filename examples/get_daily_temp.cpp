/// @file get_daily_temp.cpp
/// @brief Example: Get daily temperature prediction markets (high and low) for cities
///
/// This example demonstrates:
/// 1. Fetching series/events for daily temperature markets (both high and low)
/// 2. Getting market details with current bid/ask prices
/// 3. Querying historical candlestick (OHLC) data for price history
/// 4. (Optional) Live WebSocket streaming of prices (--stream flag or KALSHI_STREAM=1)
///
/// Data source: https://kalshi.com/category/climate/daily-temperature
///
/// Candlestick API Notes:
/// - Endpoint: GET /series/{series_ticker}/markets/{ticker}/candlesticks
/// - period_interval: 1 (1min), 60 (1hr), 1440 (1day) in MINUTES
/// - Historical data is available for markets that have trading activity
/// - Settled markets may still return historical data if within retention period
///
/// WebSocket Streaming Notes:
/// - Run with --stream or set KALSHI_STREAM=1 to enable live price updates
/// - Subscribes to orderbook_delta and trade channels for discovered markets
/// - Press Ctrl+C to stop streaming and exit cleanly

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <kalshi/kalshi.hpp>
#include <string>
#include <thread>
#include <vector>

#include "live_market_view.hpp"

// Global flag for clean shutdown
static std::atomic<bool> g_running{true};

// SIGINT handler for clean shutdown
static void signal_handler(int /*signum*/) {
	g_running = false;
}

// Rate limiter with exponential backoff
class RateLimiter {
public:
	explicit RateLimiter(std::chrono::milliseconds base_delay = std::chrono::milliseconds(150),
						 std::chrono::milliseconds max_delay = std::chrono::milliseconds(5000))
		: base_delay_(base_delay), max_delay_(max_delay), current_delay_(base_delay) {}

	// Wait before making a request, with exponential backoff on 429 errors
	void wait() {
		std::this_thread::sleep_for(current_delay_);
	}

	// Call on successful request to reset backoff
	void on_success() {
		current_delay_ = base_delay_;
	}

	// Call on 429 rate limit error to increase backoff
	void on_rate_limit() {
		current_delay_ = std::min(current_delay_ * 2, max_delay_);
		std::cerr << "[Rate limited] Backing off for " << current_delay_.count() << "ms\n";
	}

	// Check if error is a rate limit (429)
	static bool is_rate_limit_error(const kalshi::Error& err) {
		return err.http_status == 429;
	}

private:
	std::chrono::milliseconds base_delay_;
	std::chrono::milliseconds max_delay_;
	std::chrono::milliseconds current_delay_;
};

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
		const kalshi::Candlestick& c = candles[i];
		std::cout << "      │ " << std::setw(3) << (i + 1) << "  " << format_timestamp(c.timestamp)
				  << "   " << std::setw(4) << c.open_price << "  " << std::setw(4) << c.high_price
				  << "  " << std::setw(4) << c.low_price << "  " << std::setw(4) << c.close_price
				  << "  " << std::setw(5) << c.volume << "\n";
	}

	std::cout << "      └─────────────────────────────────────────────────────────────────┘\n";
}

int main(int argc, char* argv[]) {
	// Check for --stream flag or KALSHI_STREAM=1 env var
	bool stream_mode = false;
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == "--stream" || std::string(argv[i]) == "-s") {
			stream_mode = true;
		}
	}
	if (const char* env = std::getenv("KALSHI_STREAM"); env && std::string(env) == "1") {
		stream_mode = true;
	}

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
	std::cout << "║         KALSHI DAILY TEMPERATURE MARKETS - HISTORICAL DATA           ║\n";
	std::cout << "╚═══════════════════════════════════════════════════════════════════════╝\n\n";

	// Temperature series from https://kalshi.com/category/climate/daily-temperature
	// Includes both HIGH and LOW temperature markets
	std::vector<std::pair<std::string, std::string>> temp_series = {
		// High temperature series
		{"KXHIGHNY", "NYC High"},
		{"KXHIGHMIA", "Miami High"},
		{"KXHIGHLAX", "Los Angeles High"},
		{"KXHIGHAUS", "Austin High"},
		{"KXHIGHPHIL", "Philadelphia High"},
		{"KXHIGHCHI", "Chicago High"},
		{"KXHIGHDEN", "Denver High"},
		{"KXHIGHTDC", "Washington DC High"},
		{"KXHIGHTSEA", "Seattle High"},
		{"KXHIGHTLV", "Las Vegas High"},
		{"KXHIGHTSFO", "San Francisco High"},
		{"KXHIGHTNOLA", "New Orleans High"},
		// Low temperature series
		{"KXLOWTAUS", "Austin Low"},
		{"KXLOWTCHI", "Chicago Low"},
		{"KXLOWTLAX", "Los Angeles Low"},
		{"KXLOWTMIA", "Miami Low"},
		{"KXLOWTNYC", "NYC Low"},
		{"KXLOWTPHIL", "Philadelphia Low"},
		{"KXLOWTDEN", "Denver Low"},
	};

	std::cout << "Scanning " << temp_series.size() << " temperature series for active markets...\n";
	if (stream_mode) {
		std::cout << "[STREAMING MODE ENABLED - Will stream live prices after discovery]\n";
	}
	std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

	int found_count = 0;
	int total_markets = 0;
	int total_candles = 0;

	// Collect market tickers for streaming
	std::vector<std::string> discovered_tickers;

	// Rate limiter with exponential backoff (150ms base, 5s max)
	RateLimiter rate_limiter;

	// Current time for historical data queries
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	std::int64_t now_ts =
		std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
	std::int64_t seven_days_ago = now_ts - (7 * 24 * 60 * 60);

	for (const auto& [series_ticker, city_name] : temp_series) {
		// Rate limit with exponential backoff
		rate_limiter.wait();

		// Get events for this temperature series
		kalshi::GetEventsParams event_params;
		event_params.series_ticker = series_ticker;
		event_params.status = "open";

		kalshi::Result<kalshi::PaginatedResponse<kalshi::Event>> events_result =
			client.get_events(event_params);
		if (!events_result) {
			if (RateLimiter::is_rate_limit_error(events_result.error())) {
				rate_limiter.on_rate_limit();
			}
			std::cout << "  [" << series_ticker << "] " << city_name
					  << " - ERROR: " << events_result.error().message << "\n";
			continue;
		}
		rate_limiter.on_success();

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
			rate_limiter.wait();

			// Get markets for this event
			kalshi::GetMarketsParams market_params;
			market_params.event_ticker = event.event_ticker;

			kalshi::Result<kalshi::PaginatedResponse<kalshi::Market>> markets_result =
				client.get_markets(market_params);
			if (!markets_result) {
				if (RateLimiter::is_rate_limit_error(markets_result.error())) {
					rate_limiter.on_rate_limit();
				}
				std::cout << "  ✗ Failed to get markets: " << markets_result.error().message
						  << "\n\n";
				continue;
			}
			rate_limiter.on_success();

			const std::vector<kalshi::Market>& markets = markets_result->items;
			if (markets.empty()) {
				std::cout << "  ✗ No markets available\n\n";
				continue;
			}

			total_markets += static_cast<int>(markets.size());
			std::cout << "\n  Markets (" << markets.size() << " temperature brackets):\n";
			std::cout << "  ────────────────────────────────────────────────────────────────\n";

			for (const kalshi::Market& market : markets) {
				// Collect ticker for streaming
				discovered_tickers.push_back(market.ticker);

				std::cout << "    • [" << market.ticker << "] " << market.title;
				if (market.yes_bid > 0 || market.yes_ask > 0) {
					std::cout << "\n      Bid: " << market.yes_bid << "c | Ask: " << market.yes_ask
							  << "c";
				}
				std::cout << "\n";

				// Fetch historical candlestick data for each market
				rate_limiter.wait();

				kalshi::GetCandlesticksParams candle_params;
				candle_params.event_ticker = event.event_ticker; // Use event ticker, not series
				candle_params.ticker = market.ticker;
				candle_params.period_interval = 60; // 60 minutes = 1 hour candles
				candle_params.start_ts = seven_days_ago;
				candle_params.end_ts = now_ts;

				kalshi::Result<std::vector<kalshi::Candlestick>> candles_result =
					client.get_market_candlesticks(candle_params);
				if (candles_result) {
					rate_limiter.on_success();
					if (candles_result->empty()) {
						std::cout << "      [" << event.event_ticker << "/" << market.ticker
								  << "] API returned empty array (no trading data)\n";
					} else {
						total_candles += static_cast<int>(candles_result->size());
						print_candle_preview(*candles_result, event.event_ticker, market.ticker, 9);
					}
				} else {
					if (RateLimiter::is_rate_limit_error(candles_result.error())) {
						rate_limiter.on_rate_limit();
					}
					std::cout << "      [" << event.event_ticker << "/" << market.ticker << "] "
							  << "API ERROR: " << candles_result.error().message
							  << " (http: " << candles_result.error().http_status << ")\n";
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

	// WebSocket streaming mode
	if (stream_mode && !discovered_tickers.empty()) {
		std::cout
			<< "\n╔═══════════════════════════════════════════════════════════════════════╗\n";
		std::cout << "║                    LIVE WEBSOCKET STREAMING                          ║\n";
		std::cout << "╠═══════════════════════════════════════════════════════════════════════╣\n";
		std::cout << "║  Streaming " << std::setw(4) << discovered_tickers.size()
				  << " markets | Press Ctrl+C to stop                     ║\n";
		std::cout
			<< "╚═══════════════════════════════════════════════════════════════════════╝\n\n";

		// Install signal handler for clean shutdown
		std::signal(SIGINT, signal_handler);

		// Create a new signer for WebSocket (Signer is not copyable)
		kalshi::Result<kalshi::Signer> ws_signer_result =
			kalshi::Signer::from_pem_file(api_key_id, api_key_file);
		if (!ws_signer_result) {
			std::cerr << "Failed to create WebSocket signer: " << ws_signer_result.error().message
					  << "\n";
			return 1;
		}
		kalshi::Signer ws_signer = std::move(*ws_signer_result);

		// Create WebSocket client (signer must outlive the client)
		kalshi::WebSocketClient ws(ws_signer);

		// Live market view to track state
		kalshi::LiveMarketView view;
		for (const auto& ticker : discovered_tickers) {
			view.register_ticker(ticker);
		}

		// Set up callbacks
		ws.on_message([&view](const kalshi::WsMessage& msg) { view.process_message(msg); });

		ws.on_error([](const kalshi::WsError& err) {
			std::cerr << "\n[WS ERROR] Code: " << err.code << " Message: " << err.message << "\n";
		});

		ws.on_state_change([](bool connected) {
			std::cout << "\n[WS] " << (connected ? "Connected" : "Disconnected") << "\n";
		});

		// Connect
		auto conn_result = ws.connect();
		if (!conn_result) {
			std::cerr << "Failed to connect WebSocket: " << conn_result.error().message << "\n";
			return 1;
		}

		// Subscribe to orderbook and trades
		auto ob_sub = ws.subscribe_orderbook(discovered_tickers);
		if (!ob_sub) {
			std::cerr << "Failed to subscribe orderbook: " << ob_sub.error().message << "\n";
		} else {
			std::cout << "[WS] Subscribed to orderbook_delta for " << discovered_tickers.size()
					  << " markets\n";
		}

		auto trade_sub = ws.subscribe_trades(discovered_tickers);
		if (!trade_sub) {
			std::cerr << "Failed to subscribe trades: " << trade_sub.error().message << "\n";
		} else {
			std::cout << "[WS] Subscribed to trade for " << discovered_tickers.size()
					  << " markets\n";
		}

		std::cout << "\n[Press Ctrl+C to stop streaming]\n\n";

		// Main loop: print live view periodically
		auto last_print = std::chrono::steady_clock::now();
		while (g_running) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			// Print every 2 seconds
			auto now_time = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::seconds>(now_time - last_print).count() >=
				2) {
				last_print = now_time;

				// Clear screen and print header (using ANSI escape codes)
				std::cout << "\033[2J\033[H"; // Clear screen, move cursor to top
				std::cout << "╔════════════════════════════════════════════════════════════════════"
							 "═══╗\n";
				std::cout
					<< "║              LIVE TEMPERATURE MARKET PRICES                          ║\n";
				std::cout
					<< "║                 (Press Ctrl+C to stop)                               ║\n";
				std::cout << "╚════════════════════════════════════════════════════════════════════"
							 "═══╝\n\n";

				std::cout << "  Ticker                     Bid/Ask          Last Trade\n";
				std::cout
					<< "  ─────────────────────────────────────────────────────────────────────\n";

				view.print_all();
			}
		}

		// Clean shutdown
		std::cout << "\n\n[WS] Shutting down...\n";
		ws.disconnect();
		std::cout << "[WS] Disconnected. Goodbye!\n";
	} else if (stream_mode && discovered_tickers.empty()) {
		std::cout << "\n[STREAMING] No markets discovered to stream.\n";
	}

	return 0;
}
