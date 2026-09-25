// Reads public market data; no API key needed.
//
//   ./build/examples/example_market_data [SERIES_TICKER]

#include <chrono>

#include "common.hpp"

int main(int argc, char** argv) {
	const std::string series = argc > 1 ? argv[1] : "KXHIGHNY";
	kalshi::KalshiClient client{
		kalshi::HttpClient{kalshi::ClientConfig::for_environment(example::environment())}};

	const kalshi::Result<kalshi::ExchangeStatus> status = client.get_exchange_status();
	if (!status) {
		return example::fail(status.error());
	}
	std::cout << "Trading active: " << std::boolalpha << status->trading_active << "\n\n";

	kalshi::GetMarketsParams params;
	params.limit = 10;
	params.series_ticker = series;
	params.status = kalshi::GetMarketsStatus::Open;
	const kalshi::Result<kalshi::GetMarketsResponse> markets = client.get_markets(params);
	if (!markets) {
		return example::fail(markets.error());
	}
	if (markets->markets.empty()) {
		std::cout << "No open markets in " << series << '\n';
		return 0;
	}
	for (const kalshi::Market& market : markets->markets) {
		std::cout << market.ticker << "  bid " << market.yes_bid_dollars << "  ask "
				  << market.yes_ask_dollars << "  " << market.yes_sub_title << '\n';
	}

	const kalshi::Market& market = markets->markets.front();
	const kalshi::Result<kalshi::OrderbookCountFp> book =
		client.get_market_orderbook(market.ticker, {.depth = 5});
	if (!book) {
		return example::fail(book.error());
	}
	std::cout << "\nYes bids for " << market.ticker << ":\n";
	for (const kalshi::PriceLevelDollarsCountFp& level : book->yes_dollars) {
		std::cout << "  " << level[0] << " x " << level[1] << '\n';
	}

	const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
								 std::chrono::system_clock::now().time_since_epoch())
								 .count();
	const kalshi::Result<kalshi::GetMarketCandlesticksResponse> candles =
		client.get_market_candlesticks(
			series, market.ticker, {.start_ts = now - 86400, .end_ts = now, .period_interval = 60});
	if (!candles) {
		return example::fail(candles.error());
	}
	std::cout << "\n" << candles->candlesticks.size() << " hourly candles in the last day\n";
	return 0;
}
