# Include Directory

Public headers for the Kalshi C++ SDK.

## Main Header

Include all SDK functionality with:

```cpp
#include <kalshi/kalshi.hpp>
```

## Individual Headers

| Header | Purpose |
| ------ | ------- |
| `kalshi/kalshi.hpp` | Main include (includes all below) |
| `kalshi/error.hpp` | Error types and Result\<T\> |
| `kalshi/signer.hpp` | RSA-PSS authentication |
| `kalshi/http_client.hpp` | HTTP client |
| `kalshi/websocket.hpp` | WebSocket streaming |
| `kalshi/pagination.hpp` | Pagination helpers |
| `kalshi/rate_limit.hpp` | Rate limiting |
| `kalshi/retry.hpp` | Retry logic with backoff |
| `kalshi/models/market.hpp` | Market data types |
| `kalshi/models/order.hpp` | Order and trade types |

## Usage

```cpp
#include <kalshi/kalshi.hpp>

// All types are in the kalshi namespace
kalshi::Signer signer = ...;
kalshi::HttpClient client(std::move(signer));
kalshi::Result<kalshi::HttpResponse> response = client.get("/markets");
```
