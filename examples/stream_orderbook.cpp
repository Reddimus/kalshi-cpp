// Streams order book updates and trades for the given markets until Ctrl+C.
//
//   make run-stream_orderbook ARGS="MARKET_TICKER [MARKET_TICKER...]"

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

#include "common.hpp"

namespace {

std::atomic<bool> running{true};

void stop(int /*signal*/) {
	running = false;
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: example_stream_orderbook MARKET_TICKER [MARKET_TICKER...]\n";
		return 1;
	}
	std::optional<kalshi::Signer> signer = example::signer();
	if (!signer) {
		return 1;
	}
	const std::vector<std::string> tickers(argv + 1, argv + argc);

	kalshi::WebSocketClient ws(std::move(*signer),
							   kalshi::WsConfig::for_environment(example::environment()));
	ws.on_message([](const kalshi::WsMessage& message) {
		std::visit(
			[](const auto& event) { // auto-ok: visitor over the message variant
				using Event = std::decay_t<decltype(event)>;
				if constexpr (std::is_same_v<Event, kalshi::OrderbookSnapshot>) {
					std::cout << event.market_ticker << " snapshot: " << event.yes.size()
							  << " yes levels, " << event.no.size() << " no levels\n";
				} else if constexpr (std::is_same_v<Event, kalshi::OrderbookDelta>) {
					std::cout << event.market_ticker << " delta: " << event.delta_fp << " @ "
							  << event.price_dollars << '\n';
				} else if constexpr (std::is_same_v<Event, kalshi::WsTrade>) {
					std::cout << event.market_ticker << " trade: " << event.count_fp << " @ "
							  << event.yes_price_dollars << '\n';
				}
			},
			message);
	});
	ws.on_error(
		[](const kalshi::WsError& error) { std::cerr << "error: " << error.message << '\n'; });

	if (const kalshi::Result<void> connected = ws.connect(); !connected) {
		return example::fail(connected.error());
	}
	if (const kalshi::Result<kalshi::SubscriptionId> book = ws.subscribe_orderbook(tickers);
		!book) {
		return example::fail(book.error());
	}
	if (const kalshi::Result<kalshi::SubscriptionId> trades = ws.subscribe_trades(tickers);
		!trades) {
		return example::fail(trades.error());
	}

	std::signal(SIGINT, stop);
	while (running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	ws.disconnect();
	return 0;
}
