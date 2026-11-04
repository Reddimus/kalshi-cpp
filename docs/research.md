# Upstream contract

kalshi-cpp follows Kalshi's published Predictions contracts. The copies in
`spec/` are byte-identical to what Kalshi served on 2026-11-03.

| File | Version | SHA-256 |
| --- | --- | --- |
| `spec/openapi.yaml` from <https://docs.kalshi.com/openapi.yaml> | 3.35.0 | `b00862e39b9c543065c5cee75f7d16d26e682ba8c7c4cd4c329ae34d0ecded74` |
| `spec/asyncapi.yaml` from <https://docs.kalshi.com/asyncapi.yaml> | 2.0.0 | `33fd603e0f7a95acd0da82a70cfc9126c31ddc27528a6e696efb2c3806b56b23` |

Kalshi's Margin API (<https://docs.kalshi.com/perps_openapi.yaml>) has its own
host, authentication, and risk model, so this client leaves it out.

## Authentication

Each request signs `timestamp_ms + METHOD + path` with the account's key. The
path starts at `/trade-api/v2` (REST) or `/trade-api/ws/v2` (WebSocket) and
excludes the query string. The signature goes in `KALSHI-ACCESS-SIGNATURE`,
next to `KALSHI-ACCESS-KEY` and `KALSHI-ACCESS-TIMESTAMP`. RSA keys sign with
RSA-PSS and SHA-256; Ed25519 keys sign the message directly. See Kalshi's
[API keys guide](https://docs.kalshi.com/getting_started/api_keys).

## Contract limits

The upstream files contain inconsistent descriptions and incomplete constraints.
Keep the captured copies unchanged; do not treat their examples as server guarantees.
Client validation covers order-group limits, exit-trigger legs, and subscription identifiers.
Handle partial batch fills according to the operation documentation.

`Timestamp` uses Unix time, which excludes leap seconds. `parse_timestamp` returns
`ParseError` for second 60 rather than moving it to a different instant.

## Updating the contract

```bash
curl -fsS https://docs.kalshi.com/openapi.yaml -o spec/openapi.yaml
curl -fsS https://docs.kalshi.com/asyncapi.yaml -o spec/asyncapi.yaml
shasum -a 256 spec/*.yaml
make codegen test
```

If `make codegen` stops on a spec feature it can't map, extend
`tools/codegen/` to support it before committing the new spec. Review the diff
of the generated files, update the table above, and note user-visible changes
in `CHANGELOG.md`. Kalshi's changelog at
<https://docs.kalshi.com/changelog> explains most changes.
