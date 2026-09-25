# REST operations

Generated from Kalshi Predictions OpenAPI 3.31.0 (`spec/openapi.yaml`).
Each row is a `KalshiClient` method.

## account

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/account/limits` | `get_account_api_limits` |
| POST | `/account/api_usage_level/upgrade` | `upgrade_account_api_usage_level` |
| GET | `/account/api_usage_level/volume_progress` | `get_account_api_usage_level_volume_progress` |
| GET | `/account/endpoint_costs` | `get_account_endpoint_costs` |

## api-keys

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/api_keys` | `get_api_keys` |
| POST | `/api_keys` | `create_api_key` |
| POST | `/api_keys/generate` | `generate_api_key` |
| DELETE | `/api_keys/{api_key}` | `delete_api_key` |

## communications

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/communications/id` | `get_communications_id` |
| GET | `/communications/block-trade-proposals` | `get_block_trade_proposals` |
| POST | `/communications/block-trade-proposals` | `propose_block_trade` |
| POST | `/communications/block-trade-proposals/{block_trade_proposal_id}/accept` | `accept_block_trade_proposal` |
| GET | `/communications/rfqs` | `get_rfqs` |
| POST | `/communications/rfqs` | `create_rfq` |
| GET | `/communications/rfqs/{rfq_id}` | `get_rfq` |
| DELETE | `/communications/rfqs/{rfq_id}` | `delete_rfq` |
| GET | `/communications/rfqs/{rfq_id}/quotes/{quote_id}` | `get_rfq_quote` |
| DELETE | `/communications/rfqs/{rfq_id}/quotes/{quote_id}` | `delete_rfq_quote` |
| PUT | `/communications/rfqs/{rfq_id}/quotes/{quote_id}/accept` | `accept_rfq_quote` |
| PUT | `/communications/rfqs/{rfq_id}/quotes/{quote_id}/confirm` | `confirm_rfq_quote` |
| GET | `/communications/quotes` | `get_quotes` |
| POST | `/communications/quotes` | `create_quote` |
| GET | `/communications/quotes/{quote_id}` | `get_quote` (deprecated) |
| DELETE | `/communications/quotes/{quote_id}` | `delete_quote` (deprecated) |
| PUT | `/communications/quotes/{quote_id}/accept` | `accept_quote` (deprecated) |
| PUT | `/communications/quotes/{quote_id}/confirm` | `confirm_quote` (deprecated) |

## events

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/series/{series_ticker}/events/{ticker}/candlesticks` | `get_market_candlesticks_by_event` |
| GET | `/events` | `get_events` |
| GET | `/events/multivariate` | `get_multivariate_events` |
| GET | `/events/fee_changes` | `get_event_fee_changes` |
| GET | `/events/{event_ticker}` | `get_event` |
| GET | `/events/{event_ticker}/metadata` | `get_event_metadata` |
| GET | `/series/{series_ticker}/events/{ticker}/forecast_percentile_history` | `get_event_forecast_percentiles_history` |

## exchange

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/exchange/status` | `get_exchange_status` |
| GET | `/series/fee_changes` | `get_series_fee_changes` |
| GET | `/exchange/schedule` | `get_exchange_schedule` |
| GET | `/exchange/user_data_timestamp` | `get_user_data_timestamp` |

## fcm

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/fcm/fills` | `get_fcm_fills` |
| GET | `/fcm/orders` | `get_fcm_orders` |
| GET | `/fcm/subtraders` | `get_fcm_subtraders` |
| POST | `/fcm/subtraders` | `create_fcm_subtrader` |
| GET | `/fcm/subtraders/event_contract_daily_cap` | `get_fcm_event_contract_daily_cap` |
| PUT | `/fcm/subtraders/event_contract_daily_cap` | `update_fcm_event_contract_daily_cap` |
| DELETE | `/fcm/subtraders/event_contract_daily_cap` | `delete_fcm_event_contract_daily_cap` |
| GET | `/fcm/subtraders/blocked_categories` | `get_fcm_subtrader_blocked_categories` |
| PUT | `/fcm/subtraders/blocked_categories` | `update_fcm_subtrader_blocked_categories` |
| GET | `/fcm/positions` | `get_fcm_positions` |

## historical

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/historical/cutoff` | `get_historical_cutoff` |
| GET | `/historical/markets/{ticker}/candlesticks` | `get_market_candlesticks_historical` |
| GET | `/historical/fills` | `get_fills_historical` |
| GET | `/historical/orders` | `get_historical_orders` |
| GET | `/historical/positions` | `get_historical_positions` |
| GET | `/historical/trades` | `get_trades_historical` |
| GET | `/historical/markets` | `get_historical_markets` |
| GET | `/historical/markets/{ticker}` | `get_historical_market` |

## incentive-programs

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/incentive_programs` | `get_incentive_programs` |

## live-data

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/live_data/milestone/{milestone_id}` | `get_live_data_by_milestone` |
| GET | `/live_data/{type}/milestone/{milestone_id}` | `get_live_data` |
| GET | `/live_data/batch` | `get_live_datas` |
| GET | `/live_data/milestone/{milestone_id}/game_stats` | `get_game_stats` |
| GET | `/live_data/events/{event_ticker}` | `get_event_live_data` |
| GET | `/live_data/weather/{city}` | `get_weather_index` |
| GET | `/live_data/weather/{city}/calibrations` | `get_weather_index_calibrations` |

## market

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/series/{series_ticker}/markets/{ticker}/candlesticks` | `get_market_candlesticks` |
| GET | `/markets/trades` | `get_trades` |
| GET | `/markets/{ticker}/orderbook` | `get_market_orderbook` |
| GET | `/markets/orderbooks` | `get_market_orderbooks` |
| GET | `/series/{series_ticker}` | `get_series` |
| GET | `/series` | `get_series_list` |
| GET | `/markets` | `get_markets` |
| GET | `/markets/{ticker}` | `get_market` |
| GET | `/markets/candlesticks` | `batch_get_market_candlesticks` |

## milestone

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/milestones/{milestone_id}` | `get_milestone` |
| GET | `/milestones` | `get_milestones` |

## multivariate

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/multivariate_event_collections/{collection_ticker}` | `get_multivariate_event_collection` |
| POST | `/multivariate_event_collections/{collection_ticker}` | `create_market_in_multivariate_event_collection` |
| GET | `/multivariate_event_collections` | `get_multivariate_event_collections` |

## order-groups

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/portfolio/order_groups` | `get_order_groups` |
| POST | `/portfolio/order_groups/create` | `create_order_group` |
| GET | `/portfolio/order_groups/{order_group_id}` | `get_order_group` |
| DELETE | `/portfolio/order_groups/{order_group_id}` | `delete_order_group` |
| PUT | `/portfolio/order_groups/{order_group_id}/reset` | `reset_order_group` |
| PUT | `/portfolio/order_groups/{order_group_id}/trigger` | `trigger_order_group` |
| PUT | `/portfolio/order_groups/{order_group_id}/limit` | `update_order_group_limit` |

## orders

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/portfolio/orders` | `get_orders` |
| GET | `/portfolio/orders/{order_id}` | `get_order` |
| GET | `/portfolio/orders/queue_positions` | `get_order_queue_positions` |
| GET | `/portfolio/orders/{order_id}/queue_position` | `get_order_queue_position` |
| POST | `/portfolio/events/orders` | `create_order` |
| DELETE | `/portfolio/events/orders` | `cancel_all_orders` |
| POST | `/portfolio/events/orders/batched` | `batch_create_orders` |
| DELETE | `/portfolio/events/orders/batched` | `batch_cancel_orders` |
| DELETE | `/portfolio/events/orders/{order_id}` | `cancel_order` |
| POST | `/portfolio/events/orders/{order_id}/amend` | `amend_order` |
| POST | `/portfolio/events/orders/{order_id}/decrease` | `decrease_order` |

## portfolio

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/portfolio/balance` | `get_balance` |
| POST | `/portfolio/intra_exchange_instance_transfer` | `intra_exchange_instance_transfer` |
| GET | `/portfolio/intra_exchange_instance_transfers` | `get_intra_exchange_instance_transfers` |
| GET | `/portfolio/intra_exchange_instance_transfers/{transfer_id}` | `get_intra_exchange_instance_transfer` |
| POST | `/portfolio/subaccounts` | `create_subaccount` |
| POST | `/portfolio/subaccounts/transfer` | `apply_subaccount_transfer` |
| GET | `/portfolio/subaccounts/balances` | `get_subaccount_balances` |
| GET | `/portfolio/subaccounts/transfers` | `get_subaccount_transfers` |
| GET | `/portfolio/subaccounts/netting` | `get_subaccount_netting` |
| PUT | `/portfolio/subaccounts/netting` | `update_subaccount_netting` |
| GET | `/portfolio/positions` | `get_positions` |
| GET | `/portfolio/settlements` | `get_settlements` |
| GET | `/portfolio/deposits` | `get_deposits` |
| GET | `/portfolio/withdrawals` | `get_withdrawals` |
| GET | `/portfolio/summary/total_resting_order_value` | `get_portfolio_resting_order_total_value` |
| GET | `/portfolio/fills` | `get_fills` |
| GET | `/portfolio/target_balance_allocation` | `get_target_balance_allocation` |
| POST | `/portfolio/target_balance_allocation` | `set_target_balance_allocation` |

## search

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/search/tags_by_categories` | `get_tags_for_series_categories` |
| GET | `/search/filters_by_sport` | `get_filters_for_sports` |

## structured-targets

| Method | Route | C++ |
| --- | --- | --- |
| GET | `/structured_targets` | `get_structured_targets` |
| GET | `/structured_targets/{structured_target_id}` | `get_structured_target` |
