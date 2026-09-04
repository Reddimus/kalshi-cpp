# Examples

| File | Purpose |
| --- | --- |
| `basic_usage.cpp` | Configure the client and read exchange status |
| `get_markets.cpp` | List markets |
| `get_daily_temp.cpp` | Discover temperature markets and optionally stream updates |
| `live_market_view.hpp` | Maintain a thread-safe in-memory view of streamed markets |

Build from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Running an example requires `KALSHI_API_KEY_ID` and
`KALSHI_API_KEY_FILE`. The file must contain the matching RSA private key.

```bash
make run-basic_usage
make run-get_markets
make run-get_daily_temp
```

Pass `--stream` to `build/examples/example_daily_temp`, or set
`KALSHI_STREAM=1`, to enable the WebSocket view. Press Ctrl+C to stop it.

Examples make authenticated network calls. The automated test suite does not run
them.
