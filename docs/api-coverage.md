# API coverage

## REST

`KalshiClient` has a method for every operation in Kalshi Predictions OpenAPI
3.31.0: 117 operations across markets, events, series, portfolio, orders, order
groups, RFQs and quotes, block trades, subaccounts, API keys, account limits,
live data, milestones, multivariate collections, structured targets,
incentives, historical data, search, and FCM. [operations.md](operations.md)
lists them all.

The generated route tests call each operation and check its method and path.
`tests/test_operations.cpp` pins exact request bodies and response parsing for
the most used ones.

## WebSocket

| Surface | Status |
| --- | --- |
| `orderbook_delta`, `trade`, `fill`, `market_lifecycle_v2` | Supported |
| `ticker`, `market_positions`, `user_orders`, `order_group_updates`, `multivariate_market_lifecycle`, `communications`, `cfbenchmarks_value`, `cfbenchmarks_value_5hz`, `pyth_value` | Not yet typed |
| `subscribe`, `unsubscribe`, `update_subscription` (add or delete markets) | Supported |
| `list_subscriptions`, `get_snapshot`, index subscriptions | Not yet typed |

The client ignores data frames it has no model for.
