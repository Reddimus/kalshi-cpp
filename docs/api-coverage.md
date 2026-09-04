# API coverage

This table compares kalshi-cpp v0.5.2 with the official Predictions OpenAPI
3.29.0 and AsyncAPI 2.0.0 documents fetched September 3, 2026. Margin and
Perpetuals use a separate API and remain outside this library.

## REST

The public client types exchange status and schedule, core market/event/series
reads, portfolio reads, V2 orders, subaccounts, order groups, RFQs, both generic
and RFQ-scoped quote routes, API keys, milestones, structured targets,
multivariate collection reads, and incentive programs.

These Predictions operations are not typed in v0.5.2:

| Area | Method and path |
| --- | --- |
| Market data | `GET /series/fee_changes` |
| Market data | `GET /markets/candlesticks` |
| Market data | `GET /series/{series_ticker}/events/{ticker}/candlesticks` |
| Market data | `GET /events/multivariate` |
| Market data | `GET /events/fee_changes` |
| Market data | `GET /series/{series_ticker}/events/{ticker}/forecast_percentile_history` |
| Trading | `PUT /portfolio/order_groups/{order_group_id}/trigger` |
| Trading | `PUT /portfolio/order_groups/{order_group_id}/limit` |
| Trading | `POST /portfolio/intra_exchange_instance_transfer` |
| Trading | `GET /portfolio/intra_exchange_instance_transfers` |
| Trading | `GET /portfolio/intra_exchange_instance_transfers/{transfer_id}` |
| Trading | `GET /fcm/orders` |
| Trading | `GET /fcm/positions` |
| Trading | `GET /portfolio/target_balance_allocation` |
| Trading | `POST /portfolio/target_balance_allocation` |
| Communications | `GET /communications/block-trade-proposals` |
| Communications | `POST /communications/block-trade-proposals` |
| Communications | `POST /communications/block-trade-proposals/{block_trade_proposal_id}/accept` |
| Account | `POST /account/api_usage_level/upgrade` |
| Account | `GET /account/api_usage_level/volume_progress` |
| Discovery | `GET /search/tags_by_categories` |
| Discovery | `GET /search/filters_by_sport` |
| Live data | `GET /live_data/milestone/{milestone_id}` |
| Live data | `GET /live_data/{type}/milestone/{milestone_id}` |
| Live data | `GET /live_data/batch` |
| Live data | `GET /live_data/milestone/{milestone_id}/game_stats` |
| Live data | `GET /live_data/events/{event_ticker}` |
| Live data | `GET /live_data/weather/{city}` |
| Live data | `GET /live_data/weather/{city}/calibrations` |
| Multivariate | `POST /multivariate_event_collections/{collection_ticker}` |
| Archive | `GET /historical/cutoff` |
| Archive | `GET /historical/markets/{ticker}/candlesticks` |
| Archive | `GET /historical/fills` |
| Archive | `GET /historical/orders` |
| Archive | `GET /historical/positions` |
| Archive | `GET /historical/trades` |
| Archive | `GET /historical/markets` |
| Archive | `GET /historical/markets/{ticker}` |

Deprecated methods retained for source compatibility return `InvalidRequest`
before transport when their upstream route was removed.

## WebSocket

| Surface | v0.5.2 status |
| --- | --- |
| `orderbook_delta` | Supported, including current snapshot/delta exact fields |
| `trade` | Supported, including canonical direction and `ts_ms` |
| `fill` | Supported, including canonical direction, shard, fee, and position fields |
| `market_lifecycle_v2` market messages | Supported, including price ranges and nested creation metadata |
| `market_lifecycle_v2` event and fee messages | Deferred: `event_lifecycle`, `event_fee_update` |
| Other channels | Deferred: `ticker`, `market_positions`, `multivariate_market_lifecycle`, `communications`, `order_group_updates`, `user_orders`, `cfbenchmarks_value`, `cfbenchmarks_value_5hz`, `pyth_value` |
| Commands | Subscribe, unsubscribe, add markets, and delete markets supported; `get_snapshot`, `list_subscriptions`, and CF Benchmarks/Pyth update commands deferred |

The client does not silently decode deferred message types into a different
model. Unknown data frames are ignored.
