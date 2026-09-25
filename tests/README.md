# Tests

GoogleTest suites run offline. Operation tests talk to an in-memory
`HttpTransport`, `test_http_client.cpp` runs the real libcurl client against a
loopback server, and signer tests generate throwaway keys in memory.

| File | Covers |
| --- | --- |
| `test_operation_routes.cpp` | Every REST operation's method and path (generated) |
| `test_operations.cpp` | Request bodies and response parsing for common operations |
| `test_api_support.cpp` | Encoding, error mapping, null handling, validation |
| `test_http_client.cpp` | The libcurl transport, POSIX only |
| `test_transports.cpp` | Retries, token buckets, rate limiting |
| `test_signer.cpp` | Ed25519 and RSA-PSS signing |
| `test_ws_*.cpp` | WebSocket parsing, commands, and lifecycle |

Response fixtures are synthetic and follow Kalshi Predictions OpenAPI 3.31.0;
none come from a real account. Add new files to `tests/CMakeLists.txt`.
