# kalshi-cpp

Modern C++23 SDK for the [Kalshi](https://kalshi.com) prediction market API (REST + WebSocket).

## Features
- RSA-PSS request signing (Kalshi auth)
- Typed v2 REST endpoints + models
- WebSocket streaming (orderbook, trades, fills, lifecycle)
- CMake build with Makefile helpers

## Build
Prereqs: C++23 compiler, CMake 3.20+, OpenSSL, libcurl, libwebsockets.

```bash
make build
make test
```

## Usage
```cpp
#include <kalshi/kalshi.hpp>

auto signer = kalshi::Signer::from_pem_file("key-id", "/path/to/key.pem");
kalshi::HttpClient http(std::move(*signer));
kalshi::KalshiClient client(std::move(http));

auto markets = client.get_markets();
```

Docs: `docs/` (see `docs/research.md`).
