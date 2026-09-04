# Tests

GoogleTest covers models, request serialization, response parsing, query
builders, WebSocket state and parsing, and injected-transport operation
contracts. `parse_benchmark.cpp` guards request serialization and WebSocket
receive performance. `test_signer_fixture.hpp` generates its RSA key in memory;
tests do not store or use live credentials.

Run the complete suite from the repository root:

```bash
cmake -S . -B build -DKALSHI_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Add new `test_*.cpp` files to `tests/CMakeLists.txt`. Prefer an injected
`HttpTransport` for operation tests so requests remain offline and their method,
path, body, and response model are all observable.
