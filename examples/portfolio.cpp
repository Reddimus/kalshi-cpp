// Prints the balance, open positions, and resting orders. Read-only.
//
//   KALSHI_API_KEY_ID=... KALSHI_API_KEY_FILE=key.pem ./build/examples/example_portfolio

#include "common.hpp"

int main() {
	std::optional<kalshi::Signer> signer = example::signer();
	if (!signer) {
		return 1;
	}
	kalshi::KalshiClient client{kalshi::HttpClient{
		std::move(*signer), kalshi::ClientConfig::for_environment(example::environment())}};

	const kalshi::Result<kalshi::GetBalanceResponse> balance = client.get_balance();
	if (!balance) {
		return example::fail(balance.error());
	}
	std::cout << "Balance: $" << balance->balance_dollars << '\n';

	const kalshi::Result<kalshi::GetPositionsResponse> positions =
		client.get_positions({.count_filter = "position"});
	if (!positions) {
		return example::fail(positions.error());
	}
	std::cout << positions->market_positions.size() << " open positions\n";
	for (const kalshi::MarketPosition& position : positions->market_positions) {
		std::cout << "  " << position.ticker << "  " << position.position_fp << " contracts, "
				  << "exposure $" << position.market_exposure_dollars << '\n';
	}

	const kalshi::Result<kalshi::GetOrdersResponse> orders =
		client.get_orders({.status = "resting"});
	if (!orders) {
		return example::fail(orders.error());
	}
	std::cout << orders->orders.size() << " resting orders\n";
	for (const kalshi::Order& order : orders->orders) {
		std::cout << "  " << order.ticker << "  " << kalshi::to_string(order.book_side) << ' '
				  << order.remaining_count_fp << " @ " << order.yes_price_dollars << '\n';
	}
	return 0;
}
