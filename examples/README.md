# Examples

| Program | What it does |
| --- | --- |
| `market_data.cpp` | Markets, an order book, and candlesticks, without a key |
| `portfolio.cpp` | Balance, positions, and resting orders |
| `place_and_cancel_order.cpp` | Places and cancels an order on the demo exchange; refuses production |
| `stream_orderbook.cpp` | Prints order book deltas and trades until Ctrl+C |

They build with the project (`build/examples/example_<name>`). Run one with
`make run-<name> ARGS="..."`, which loads settings from `.env`:

```bash
KALSHI_API_KEY_ID=...                     # key ID from kalshi.com/account/profile
KALSHI_API_KEY_FILE=/path/to/kalshi.key   # matching private key, PEM
KALSHI_ENV=demo                           # optional; production otherwise
```

These programs call the live API. The test suite never runs them.
