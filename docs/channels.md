# WebSocket channels

Covers Kalshi's AsyncAPI 2.0.0 document (`spec/asyncapi.yaml`).
Subscribe with `WebSocketClient::subscribe(ws::Channel::..., params)`; each message
arrives in `on_message` as `ws::Update<T>`.

Keep one `OrderbookDelta` handle per client. Use `add_markets` to extend it.
A second `subscribe` returns `InvalidRequest` while an orderbook handle remains
registered. Unsubscribing removes an unsent handle immediately; a rejected
subscribe also removes its handle. Otherwise, wait for `Unsubscribed` or
disconnect before replacing it. Updates are rejected while unsubscribe is
pending; if the server rejects unsubscribe, retry the update on the same handle.
Set `update_interval_ms` to `V0`, `V100`, or `V1000`, or omit it for zero milliseconds.

| Channel | `ws::Channel` | Messages |
| --- | --- | --- |
| `orderbook_delta` | `OrderbookDelta` | `ws::OrderbookSnapshot`, `ws::OrderbookDelta` |
| `ticker` | `Ticker` | `ws::Ticker` |
| `trade` | `Trade` | `ws::Trade` |
| `fill` | `Fill` | `ws::Fill` |
| `market_positions` | `MarketPositions` | `ws::MarketPosition` |
| `market_lifecycle_v2` | `MarketLifecycleV2` | `ws::EventLifecycle`, `ws::EventFeeUpdate`, `ws::MarketMetadataUpdated`, `ws::MarketLifecycleV2` |
| `multivariate_market_lifecycle` | `MultivariateMarketLifecycle` | `ws::EventLifecycle`, `ws::MultivariateMarketLifecycle` |
| `communications` | `Communications` | `ws::RfqCreated`, `ws::RfqDeleted`, `ws::QuoteCreated`, `ws::QuoteAccepted`, `ws::QuoteExecuted`, `ws::RfqBatchCreated`, `ws::RfqBatchDeleted`, `ws::BatchQuoteCreated`, `ws::RfqBatchBestQuotes`, `ws::BatchQuoteAccepted`, `ws::RfqBatchExecuted`, `ws::BatchQuoteExecuted` |
| `order_group_updates` | `OrderGroupUpdates` | `ws::OrderGroupUpdates` |
| `user_orders` | `UserOrders` | `ws::UserOrder` |
| `cfbenchmarks_value` | `CfbenchmarksValue` | `ws::CfbenchmarksValue`, `ws::CfbenchmarksIndexList` |
| `cfbenchmarks_value_5hz` | `CfbenchmarksValue5hz` | `ws::CfbenchmarksValue5Hz`, `ws::Cfbenchmarks5HzIndexList` |
| `pyth_value` | `PythValue` | `ws::PythValue`, `ws::PythUnderlyingList` |
