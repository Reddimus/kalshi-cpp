# Upstream contract

kalshi-cpp follows Kalshi's published Predictions contracts. The copies in
`spec/` are byte-identical to what Kalshi served on 2026-09-25.

| File | Version | SHA-256 |
| --- | --- | --- |
| `spec/openapi.yaml` from <https://docs.kalshi.com/openapi.yaml> | 3.31.0 | `7a870e939ec61793ff31d04e89c40a85246a82d5a60e0f3803a45806444baa97` |
| `spec/asyncapi.yaml` from <https://docs.kalshi.com/asyncapi.yaml> | 2.0.0 | `1fe32a4b7c63fe6b98b09cdb8a2510ae14cc09764c0679feb7d2fe772a023537` |

Kalshi's Margin API (<https://docs.kalshi.com/perps_openapi.yaml>) has its own
host, authentication, and risk model, so this client leaves it out.

## Authentication

Each request signs `timestamp_ms + METHOD + path` with the account's key. The
path starts at `/trade-api/v2` (REST) or `/trade-api/ws/v2` (WebSocket) and
excludes the query string. The signature goes in `KALSHI-ACCESS-SIGNATURE`,
next to `KALSHI-ACCESS-KEY` and `KALSHI-ACCESS-TIMESTAMP`. RSA keys sign with
RSA-PSS and SHA-256; Ed25519 keys sign the message directly. See Kalshi's
[API keys guide](https://docs.kalshi.com/getting_started/api_keys).

## Updating the contract

```bash
curl -fsS https://docs.kalshi.com/openapi.yaml -o spec/openapi.yaml
curl -fsS https://docs.kalshi.com/asyncapi.yaml -o spec/asyncapi.yaml
shasum -a 256 spec/*.yaml
make codegen test
```

Review the diff of the generated files, update the table above, and note
user-visible changes in `CHANGELOG.md`. Kalshi's changelog at
<https://docs.kalshi.com/changelog> explains most changes.
