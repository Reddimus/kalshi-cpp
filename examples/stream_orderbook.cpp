// Keeps a local order book for each market from snapshots and deltas, and
// prints the best yes and no bids as they change. Ctrl+C stops it.
//
//   make run-stream_orderbook ARGS="MARKET_TICKER [MARKET_TICKER...]"
//
// When a delta goes missing, the client reports the gap to on_error and asks
// for fresh snapshots, which replace the books below. After a reconnect it
// resubscribes, and the server sends snapshots again.

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "common.hpp"
#include "orderbook.hpp"

namespace {

std::atomic<bool> running{true};

void stop(int /*signal*/) {
	running = false;
}

using example::Book;
using example::Levels;

std::string best(const Levels& levels) {
	if (levels.empty()) {
		return "none";
	}
	const std::pair<const std::int64_t, std::int64_t>& top = *levels.rbegin();
	std::array<char, 48> text{};
	std::snprintf(text.data(), text.size(), "%.4f x %.2f", static_cast<double>(top.first) / 1e4,
				  static_cast<double>(top.second) / 1e2);
	return text.data();
}

void show(std::string_view ticker, const Book& book) {
	std::cout << ticker << "  yes bid " << best(book.yes) << "  |  no bid " << best(book.no)
			  << '\n';
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

	example::Orderbooks books; // touched only on the client's network thread
	kalshi::ws::Subscription subscription;
	kalshi::WebSocketClient ws(std::move(*signer),
							   kalshi::WsConfig::for_environment(example::environment()));
	ws.on_message([&books, &ws, &subscription](const kalshi::WsMessage& message) {
		const example::Orderbooks::Change change = books.consume(message);
		if (change.needs_snapshot) {
			std::cerr << "Invalid orderbook; requesting a snapshot for " << change.ticker << '\n';
			if (const kalshi::Result<void> requested =
					ws.request_snapshot(subscription, {std::string(change.ticker)});
				!requested) {
				std::cerr << "Snapshot request failed: " << requested.error().message << '\n';
			}
		} else if (change.book) {
			show(change.ticker, *change.book);
		}
	});
	ws.on_error(
		[](const kalshi::WsError& error) { std::cerr << "error: " << error.message << '\n'; });
	ws.on_state_change([](kalshi::WsState state) {
		std::cerr << "connection: " << kalshi::to_string(state) << '\n';
	});

	// Subscriptions made before connect() are sent once the connection is up.
	kalshi::ws::SubscribeParams params;
	params.market_tickers.assign(argv + 1, argv + argc);
	const kalshi::Result<kalshi::ws::Subscription> book =
		ws.subscribe(kalshi::ws::Channel::OrderbookDelta, std::move(params));
	if (!book)
		return example::fail(book.error());
	subscription = *book;
	if (const kalshi::Result<void> connected = ws.connect(); !connected) {
		return example::fail(connected.error());
	}

	std::signal(SIGINT, stop);
	while (running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	ws.disconnect();
	return 0;
}
