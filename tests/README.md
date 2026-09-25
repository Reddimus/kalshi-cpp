# Tests

GoogleTest suites run offline. Operation tests talk to an in-memory
`HttpTransport`, `test_http_client.cpp` and `test_ws_client.cpp` run the real
clients against local servers, and signer tests generate throwaway keys in
memory.

| File | Covers |
| --- | --- |
| `test_operation_routes.cpp` | Every REST operation's method and path (generated) |
| `test_operations.cpp` | Request bodies and response parsing for common operations |
| `test_api_support.cpp` | Encoding, error mapping, null handling, validation |
| `test_http_client.cpp` | The libcurl transport, POSIX only |
| `test_transports.cpp` | Retries, token buckets, rate limiting |
| `test_signer.cpp` | Ed25519 and RSA-PSS signing |
| `test_helpers.cpp` | Cents, contracts, timestamps, order directions, pagination |
| `test_fixed_point.cpp` | Exact decimal parsing and scaling |
| `test_features.cpp` | Defaults for configs and enums |
| `test_version.cpp` | `kalshi::VERSION` and its components |
| `parse_benchmark.cpp` | A coarse throughput guard, run as its own test |
| `test_allocation_counter.cpp` | The `operator new` counter in `support/`, in its own binary |
| `test_ws_messages.cpp` | Every example frame in the AsyncAPI spec (generated) |
| `test_ws_frames.cpp` | Control frames, discriminated types, nulls, unknown values |
| `test_ws_subscriptions.cpp` | Command frames, held commands, resubscribing, gaps |
| `test_ws_client.cpp` | The WebSocket client against a local server |
| `test_ws_lifecycle.cpp` | URL validation, moved-from clients, concurrent calls |

Response fixtures are synthetic and follow Kalshi Predictions OpenAPI 3.31.0;
none come from a real account. Add new files to `tests/CMakeLists.txt`.
