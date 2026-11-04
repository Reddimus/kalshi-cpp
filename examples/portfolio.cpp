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

	kalshi::GetPositionsParams positions_params{.count_filter = "position"};
	const kalshi::Result<std::vector<kalshi::MarketPosition>> positions = kalshi::collect_pages(
		[&](std::string_view cursor) {
			positions_params.cursor =
				cursor.empty() ? std::nullopt : std::optional<std::string>{cursor};
			return client.get_positions(positions_params);
		},
		&kalshi::GetPositionsResponse::market_positions);
	if (!positions) {
		return example::fail(positions.error());
	}
	std::cout << positions->size() << " open positions\n";
	for (const kalshi::MarketPosition& position : *positions) {
		std::cout << "  " << position.ticker << "  " << position.position_fp << " contracts, "
				  << "exposure $" << position.market_exposure_dollars << '\n';
	}

	kalshi::GetOrdersParams orders_params{.status = "resting"};
	const kalshi::Result<std::vector<kalshi::Order>> orders = kalshi::collect_pages(
		[&](std::string_view cursor) {
			orders_params.cursor =
				cursor.empty() ? std::nullopt : std::optional<std::string>{cursor};
			return client.get_orders(orders_params);
		},
		&kalshi::GetOrdersResponse::orders);
	if (!orders) {
		return example::fail(orders.error());
	}
	std::cout << orders->size() << " resting orders\n";
	for (const kalshi::Order& order : *orders) {
		std::cout << "  " << order.ticker << "  " << kalshi::to_string(order.book_side) << ' '
				  << order.remaining_count_fp << " @ " << order.yes_price_dollars << '\n';
	}
	return 0;
}
