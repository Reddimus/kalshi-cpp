# Upstream contract provenance

kalshi-cpp targets Kalshi's Predictions API. The contract was revalidated on
2026-09-03 against the official primary sources below.

| Contract | Version | SHA-256 |
| --- | --- | --- |
| [Predictions OpenAPI](https://docs.kalshi.com/openapi.yaml) | 3.29.0 | `75d99f2579b890cc1ae3d8b4c194722415bccf2e79cbde0a2f09d9e8835ce680` |
| [Margin OpenAPI](https://docs.kalshi.com/perps_openapi.yaml) | 0.0.1 | `ba4d1e724a7fc66306f3e2fe4a892d8551e69fc577886735b026580f4313921c` |

The contracts are separate products. Margin endpoints are deliberately not
mixed into `KalshiClient`; they have different host, authentication, and risk
semantics.

## Contract rules

- Treat OpenAPI route, verb, parameter name, and required-body declarations as
  authoritative.
- Preserve fixed-point dollar and count strings exactly.
- Convert to legacy integer fields only when conversion is exact and in range.
- Include `exchange_index` in portfolio filters and models where documented.
- Fail locally when a removed operation cannot be represented safely.
- Test supported operations through an injected `HttpTransport`; no live
  trading credential is required for contract tests.

## Refresh procedure

```bash
curl --fail --silent --show-error \
  https://docs.kalshi.com/openapi.yaml -o /tmp/kalshi-openapi.yaml
shasum -a 256 /tmp/kalshi-openapi.yaml
rg '^  /' /tmp/kalshi-openapi.yaml
```

Compare the operation list and component schemas with `include/kalshi/api.hpp`,
`src/api/client.cpp`, and `tests/test_operation_contracts.cpp`. Record a new
date, version, and digest whenever the checked contract changes.

The Predictions document currently contains 109 operations. The README lists
the operation groups that still need typed methods. Do not infer support from a
route family alone; each public method needs a request, response, and injected
transport contract test.
