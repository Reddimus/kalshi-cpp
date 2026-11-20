# Upstream contract

kalshi-cpp follows Kalshi's published Predictions contracts. The copies in
`spec/` are byte-identical to what Kalshi served on 2026-11-20.

| File | Version | SHA-256 |
| --- | --- | --- |
| `spec/openapi.yaml` from <https://docs.kalshi.com/openapi.yaml> | 3.37.0 | `ec29b939bfe21a60ca1a11fa173389118c06b4d01bd59898f66f37d64046e43a` |
| `spec/asyncapi.yaml` from <https://docs.kalshi.com/asyncapi.yaml> | 2.0.0 | `dbd168af1f6ff2ec397ae348bf806288512abd36aff0528ee3ea7bb0d085507c` |

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
