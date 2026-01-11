# Kalshi API Research

## Overview

This document summarizes findings from analyzing the official Kalshi SDK implementations (TypeScript npm package and Python PyPI package).

## SDK Versions Analyzed

- **TypeScript SDK**: `kalshi@0.0.5` on npm (WebSocket streaming only)
- **Python SDK**: `kalshi-python@2.1.4` on PyPI (Full REST API, OpenAPI-generated)

## API Endpoints

### Production Endpoints

- **REST API**: `https://api.elections.kalshi.com/trade-api/v2/`
- **WebSocket**: `wss://api.elections.kalshi.com/trade-api/ws/v2`

### Legacy Endpoints (deprecated)

- `https://trading-api.kalshi.com/v1` (Python SDK v0.2.0)

## Authentication

### RSA-PSS Signing (TypeScript SDK - Current)

The TypeScript SDK uses RSA-PSS signatures for authentication:

```mermaid
sequenceDiagram
    participant Client
    participant Kalshi API
    
    Client->>Client: Generate timestamp (milliseconds)
    Client->>Client: Create message: timestamp + method + path
    Client->>Client: Sign with RSA-PSS (SHA-256, PSS padding, salt=digest length)
    Client->>Kalshi API: Request with headers
    Note right of Client: KALSHI-ACCESS-KEY: api_key_id<br/>KALSHI-ACCESS-SIGNATURE: base64(signature)<br/>KALSHI-ACCESS-TIMESTAMP: timestamp
    Kalshi API->>Client: Response
```

#### Signature Algorithm Details

- **Algorithm**: RSA-PSS
- **Hash**: SHA-256
- **Padding**: RSA_PKCS1_PSS_PADDING
- **Salt Length**: RSA_PSS_SALTLEN_DIGEST (same as hash output, 32 bytes)
- **Message Format**: `{timestamp}{method}{path}` (no separators)
- **Timestamp**: Unix milliseconds as string
- **Output**: Base64-encoded signature

### Session-Based Auth (Python SDK - Legacy v1 API)

The Python SDK uses email/password login with session tokens:

1. POST to `/log_in` with email/password
2. Receive token in response
3. Use `Authorization: Basic {token}` header for subsequent requests

## WebSocket Channels

### Available Channels

1. `orderbook_delta` - Order book updates
2. `trade` - Trade executions
3. `fill` - User fill notifications
4. `market_lifecycle` - Market status changes

### Message Types

#### Subscribe Command

```json
{
  "id": 1,
  "cmd": "subscribe",
  "params": {
    "channels": ["orderbook_delta"],
    "market_tickers": ["TICKER-1", "TICKER-2"]
  }
}
```

#### Subscribed Response

```json
{
  "id": 1,
  "type": "subscribed",
  "msg": {
    "sid": 1,
    "channel": "orderbook_delta"
  }
}
```

#### Update Subscription Command

```json
{
  "id": 2,
  "cmd": "update_subscription",
  "params": {
    "action": "add_markets",
    "channel": "orderbook_delta",
    "market_tickers": ["TICKER-3"],
    "sids": [1]
  }
}
```

#### Unsubscribe Command

```json
{
  "id": 3,
  "cmd": "unsubscribe",
  "params": {
    "sids": [1]
  }
}
```

#### Orderbook Snapshot

```json
{
  "type": "orderbook_snapshot",
  "sid": 1,
  "seq": 1,
  "msg": {
    "market_ticker": "TICKER",
    "yes": [[50, 100], [51, 200]],
    "no": [[49, 150]]
  }
}
```

#### Orderbook Delta

```json
{
  "type": "orderbook_delta",
  "sid": 1,
  "seq": 2,
  "msg": {
    "market_ticker": "TICKER",
    "price": 50,
    "delta": -50,
    "side": "yes"
  }
}
```

#### Trade

```json
{
  "type": "trade",
  "sid": 1,
  "msg": {
    "trade_id": "uuid",
    "market_ticker": "TICKER",
    "yes_price": 50,
    "no_price": 50,
    "count": 10,
    "taker_side": "yes",
    "ts": 1234567890
  }
}
```

#### Fill

```json
{
  "type": "fill",
  "sid": 1,
  "msg": {
    "trade_id": "uuid",
    "order_id": "uuid",
    "market_ticker": "TICKER",
    "is_taker": true,
    "side": "yes",
    "yes_price": 50,
    "no_price": 50,
    "count": 10,
    "action": "buy",
    "ts": 1234567890
  }
}
```

#### Market Lifecycle

```json
{
  "type": "market_lifecycle",
  "sid": 1,
  "msg": {
    "market_ticker": "TICKER",
    "open_ts": 1234567890,
    "close_ts": 1234567890,
    "determination_ts": 1234567890,
    "settled_ts": 1234567890,
    "result": "yes",
    "is_deactivated": false
  }
}
```

## REST API Endpoints (v2)

Based on the Python SDK kalshi-python@2.1.4 analysis:

### Exchange API

- `GET /exchange/status` - Get exchange status
- `GET /exchange/schedule` - Get exchange schedule
- `GET /exchange/announcements` - Get announcements

### Markets API

- `GET /markets` - List markets (paginated)
- `GET /markets/{ticker}` - Get market details
- `GET /markets/{ticker}/orderbook` - Get order book
- `GET /markets/{ticker}/candlesticks` - Get candlestick data
- `GET /trades` - Get public trades (paginated)

### Events API

- `GET /events` - List events (paginated)
- `GET /events/{event_ticker}` - Get event details
- `GET /events/{event_ticker}/metadata` - Get event metadata

### Series API

- `GET /series` - List series
- `GET /series/{series_ticker}` - Get series details

### Portfolio API (Authenticated)

- `GET /portfolio/balance` - Get account balance
- `GET /portfolio/positions` - Get positions (paginated)
- `GET /portfolio/orders` - Get orders (paginated)
- `GET /portfolio/orders/{order_id}` - Get single order
- `GET /portfolio/fills` - Get fills (paginated)
- `GET /portfolio/settlements` - Get settlements (paginated)

### Order Management (Authenticated)

- `POST /portfolio/orders` - Create order
- `DELETE /portfolio/orders/{order_id}` - Cancel order
- `POST /portfolio/orders/{order_id}/amend` - Amend order
- `POST /portfolio/orders/{order_id}/decrease` - Decrease order
- `POST /portfolio/orders/batched` - Batch create orders
- `DELETE /portfolio/orders/batched` - Batch cancel orders

## Data Models

### Market

```typescript
interface Market {
  ticker: string;
  series_ticker?: string;
  event_ticker?: string;
  title: string;
  subtitle?: string;
  open_time: datetime;
  close_time: datetime;
  expiration_time?: datetime;
  status: "initialized" | "active" | "closed" | "settled" | "determined";
  yes_bid?: number;
  yes_ask?: number;
  no_bid?: number;
  no_ask?: number;
  last_price?: number;
  volume?: number;
  volume_24h?: number;
  result?: "yes" | "no" | "";
  can_close_early?: boolean;
  cap_count?: number;
}
```

### Order

```typescript
interface CreateOrderRequest {
  ticker: string;
  client_order_id?: string;
  side: "yes" | "no";
  action: "buy" | "sell";
  count: number;  // >= 1
  type: "limit" | "market";
  yes_price?: number;  // 1-99
  no_price?: number;   // 1-99
  expiration_ts?: number;
  sell_position_floor?: number;
  buy_max_cost?: number;
}
```

### Price

- Prices are in cents (1-99 range for binary markets)
- Integer type

### Order Book Entry

- Tuple: `[price_cents, quantity_contracts]`

### Side

- Enum: `"yes"` | `"no"`

### Action

- Enum: `"buy"` | `"sell"`

## Error Handling

### HTTP Error Response

```json
{
  "error": {
    "code": "error_code",
    "message": "Human-readable error message"
  }
}
```

### WebSocket Error Response

```json
{
  "id": 1,
  "type": "error",
  "msg": {
    "code": 400,
    "message": "Error description"
  }
}
```

## References

- TypeScript SDK: `kalshi@0.0.5` on npm
- Python SDK: `kalshi-python@2.1.4` on PyPI
- API Documentation: <https://kalshi.com/docs/api>
- OpenAPI Generator: Python SDK is auto-generated from OpenAPI spec
