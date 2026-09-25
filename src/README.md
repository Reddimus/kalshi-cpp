# Source layout

| Path | Contents | CMake target |
| --- | --- | --- |
| `api/` | `KalshiClient`, request plumbing, and helpers | `kalshi_api` |
| `api/operations/` | One generated file per API tag. Do not edit | `kalshi_api` |
| `auth/` | Ed25519 and RSA-PSS request signing | `kalshi_auth` |
| `http/` | libcurl transport, retries, and rate limiting | `kalshi_http` |
| `models/` | Model helpers and the generated enum adapters | `kalshi_models` |
| `ws/` | libwebsockets client, subscriptions, and frame parsing | `kalshi_ws` |
| `json.hpp`, `json_nulls.*` | Private Glaze setup shared by the targets above | |

`api/validate.hpp`, `models/json_meta.hpp`, and `ws/wire.hpp` are generated
too. Public headers live in `include/kalshi/`; anything only the
implementation needs stays here.
