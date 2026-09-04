# Public headers

Include the complete SDK with:

```cpp
#include <kalshi/kalshi.hpp>
```

| Header | Purpose |
| --- | --- |
| `kalshi/api.hpp` | Typed Predictions REST client and models |
| `kalshi/error.hpp` | `Error` and `Result<T>` |
| `kalshi/fixed_point.hpp` | Checked decimal conversion |
| `kalshi/http_client.hpp` | Signed libcurl transport |
| `kalshi/pagination.hpp` | Cursor pagination helpers |
| `kalshi/rate_limit.hpp` | Token-bucket rate limiting |
| `kalshi/retry.hpp` | Retry policy and backoff |
| `kalshi/signer.hpp` | RSA-PSS request signing |
| `kalshi/websocket.hpp` | WebSocket client and message models |
| `kalshi/models/` | Market and order models |

Headers under `kalshi/detail/` support the implementation and tests. They are
not stable consumer interfaces.
