# Fixture provenance

The response bodies in `test_operation_contracts.cpp` and
`test_response_parsers.cpp` are synthetic, non-account fixtures shaped from
Kalshi Predictions OpenAPI 3.29.0, fetched 2026-09-03.

- Source: <https://docs.kalshi.com/openapi.yaml>
- SHA-256: `75d99f2579b890cc1ae3d8b4c194722415bccf2e79cbde0a2f09d9e8835ce680`
- Credentials or production account data: none

The fixtures deliberately include fractional prices and counts that cannot be
represented by the deprecated integer fields. This verifies that parsing never
rounds or saturates current fixed-point values.
