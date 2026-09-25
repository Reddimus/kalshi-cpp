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
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "common.hpp"

namespace {

std::atomic<bool> running{true};

void stop(int /*signal*/) {
	running = false;
}

// Prices in ten-thousandths of a dollar, counts in hundredths of a contract.
using Levels = std::map<std::int64_t, std::int64_t>;

struct Book {
	Levels yes;
	Levels no;
};

std::int64_t scaled(const std::string& text, std::uint8_t scale) {
	const kalshi::Result<kalshi::FixedPoint> value = kalshi::FixedPoint::parse(text);
	return value ? value->scaled_integer(scale).value_or(0) : 0;
}

void load(Levels& levels, const std::optional<std::vector<std::array<std::string, 2>>>& snapshot) {
	levels.clear();
	for (const std::array<std::string, 2>& level :
		 snapshot.value_or(std::vector<std::array<std::string, 2>>{})) {
		levels[scaled(level[0], 4)] = scaled(level[1], 2);
	}
}

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

void show(const std::string& ticker, const Book& book) {
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

	std::map<std::string, Book> books; // touched only on the client's network thread
	kalshi::WebSocketClient ws(std::move(*signer),
							   kalshi::WsConfig::for_environment(example::environment()));
	ws.on_message([&books](const kalshi::WsMessage& message) {
		using Snapshot = kalshi::ws::Update<kalshi::ws::OrderbookSnapshot>;
		using Delta = kalshi::ws::Update<kalshi::ws::OrderbookDelta>;
		if (const Snapshot* snapshot = std::get_if<Snapshot>(&message)) {
			Book& book = books[snapshot->msg.market_ticker];
			load(book.yes, snapshot->msg.yes_dollars_fp);
			load(book.no, snapshot->msg.no_dollars_fp);
			show(snapshot->msg.market_ticker, book);
		} else if (const Delta* delta = std::get_if<Delta>(&message)) {
			Book& book = books[delta->msg.market_ticker];
			Levels& side = delta->msg.side == kalshi::Side::Yes ? book.yes : book.no;
			const std::int64_t price = scaled(delta->msg.price_dollars, 4);
			std::int64_t& count = side[price];
			count += scaled(delta->msg.delta_fp, 2);
			if (count <= 0) {
				side.erase(price);
			}
			show(delta->msg.market_ticker, book);
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
	if (const kalshi::Result<kalshi::ws::Subscription> book =
			ws.subscribe(kalshi::ws::Channel::OrderbookDelta, std::move(params));
		!book) {
		return example::fail(book.error());
	}
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
