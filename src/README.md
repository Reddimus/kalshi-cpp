# Source layout

Implementation code is split by dependency boundary.

| Path | Purpose | CMake target |
| --- | --- | --- |
| `api/` | Typed Predictions operations, parsers, and request bodies | `kalshi_api` |
| `auth/` | RSA-PSS request signing | `kalshi_auth` |
| `core/` | Error, rate-limit, and retry support | `kalshi_core` |
| `http/` | Signed libcurl transport | `kalshi_http` |
| `models/` | Out-of-line model code | `kalshi_models` |
| `ws/` | libwebsockets client, commands, and connection state | `kalshi_ws` |

Build and test from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Public interfaces belong in `include/kalshi/`. Keep wire-only structs and
serialization helpers here unless consumers need them.
