// Places a resting limit order and cancels it. Runs only against the demo
// exchange, whose keys and balances are separate from production.
// With KALSHI_ENV=demo and a demo key in .env:
//
//   make run-place_and_cancel_order ARGS=MARKET_TICKER

#include "common.hpp"

int main(int argc, char** argv) {
	if (example::environment() != kalshi::Environment::Demo) {
		std::cerr << "This example places orders. Set KALSHI_ENV=demo and use a demo key.\n";
		return 1;
	}
	if (argc < 2) {
		std::cerr << "Usage: example_place_and_cancel_order MARKET_TICKER\n";
		return 1;
	}
	std::optional<kalshi::Signer> signer = example::signer();
	if (!signer) {
		return 1;
	}
	kalshi::KalshiClient client{kalshi::HttpClient{
		std::move(*signer), kalshi::ClientConfig::for_environment(kalshi::Environment::Demo)}};

	// A one-cent post-only bid rests on the book without filling.
	kalshi::CreateOrderV2Request order;
	order.ticker = argv[1];
	order.side = kalshi::BookSide::Bid;
	order.count = "1.00";
	order.price = "0.0100";
	order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
	order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
	order.post_only = true;
	const kalshi::Result<kalshi::CreateOrderV2Response> placed = client.create_order(order);
	if (!placed) {
		return example::fail(placed.error());
	}
	std::cout << "Placed " << placed->order_id << ", " << placed->remaining_count << " resting\n";

	const kalshi::Result<kalshi::CancelOrderV2Response> canceled =
		client.cancel_order(placed->order_id, {.market_ticker = order.ticker});
	if (!canceled) {
		return example::fail(canceled.error());
	}
	std::cout << "Canceled " << canceled->reduced_by << " contracts\n";
	return 0;
}
