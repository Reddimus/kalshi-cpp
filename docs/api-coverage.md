# API coverage

## REST

`KalshiClient` has a method for every operation in Kalshi Predictions OpenAPI
3.35.0: 133 operations across markets, events, series, portfolio, orders, order
groups, RFQs and quotes, block trades, subaccounts, API keys, account limits,
live data, milestones, multivariate collections, structured targets,
incentives, historical data, search, and FCM. [operations.md](operations.md)
lists them all. Ten RFQ-batch operations and four event-contract exit-trigger operations
are present in the served spec but marked prelaunch by Kalshi; availability
depends on the exchange and account.
Contract coverage does not mean every endpoint has been exercised live.

The generated route tests call each operation. Valid requests check their method
and path; requests with required fields left empty check local rejection.
`tests/test_operations.cpp` pins exact request bodies and response parsing for
the most used ones.

## WebSocket

`WebSocketClient` handles every channel and command in Kalshi's AsyncAPI
document: all 13 channels, `subscribe`, `unsubscribe`, `update_subscription`
(markets, snapshots, CF Benchmarks indices, Pyth underlyings), and
`list_subscriptions`. [channels.md](channels.md) maps each channel to its
message types.

Every example frame in the AsyncAPI document parses in `tests/test_ws_messages.cpp`.
`tests/test_ws_client.cpp` runs the client against a local server to cover the
handshake, acknowledgements, reconnects, and sequence gaps.

Optional `sending_ts_ms` values are preserved on data and control messages.
