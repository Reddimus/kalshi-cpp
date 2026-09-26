# Public headers

`#include <kalshi/kalshi.hpp>` brings in everything.

| Header | Contents |
| --- | --- |
| `kalshi/api.hpp` | `KalshiClient`, one method per REST operation |
| `kalshi/models.hpp` | Request, response, and enum types from the OpenAPI spec |
| `kalshi/helpers.hpp` | Fixed-point to cents or contracts, timestamps, order directions |
| `kalshi/http_client.hpp` | Transport interface, libcurl client, `ClientConfig` |
| `kalshi/retry.hpp` | `RetryingTransport` and `RetryPolicy` |
| `kalshi/rate_limit.hpp` | `TokenBucket` and `RateLimitedTransport` |
| `kalshi/pagination.hpp` | `collect_pages` |
| `kalshi/signer.hpp` | API key loading and request signing |
| `kalshi/websocket.hpp` | `WebSocketClient`, `WsConfig`, and `WsError` |
| `kalshi/ws_models.hpp` | WebSocket channels and message types from the AsyncAPI spec |
| `kalshi/error.hpp` | `Error`, `ErrorCode`, and `Result<T>` |
| `kalshi/environment.hpp` | Production and demo URLs |
| `kalshi/fixed_point.hpp` | Exact decimal parsing |
| `kalshi/raw_json.hpp` | `RawJson`, free-form JSON kept as text |
| `kalshi/version.hpp` | `kalshi::VERSION`, set at configure time |

Headers under `kalshi/detail/` support the implementation and tests. They are
not a stable interface.
