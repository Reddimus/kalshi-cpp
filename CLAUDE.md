# kalshi-cpp development guide

## Required gates

Run `make test`, `make lint`, and `./tools/test_consumers.sh` before review.
`make lint` needs clang-format 18 and PyYAML.
User-visible changes also require a `CHANGELOG.md` entry. The CI workflow is
the source of truth for Linux, macOS, Windows, sanitizer, clang-tidy, Markdown,
and consumer gates.

Examples call the live API, and most need a key. Keep automated tests offline
by injecting an `HttpTransport` or using the local servers in `tests/`.

## Architecture

- Targets are layered as core, auth, HTTP, models, WebSocket, API, then the
  `kalshi` interface target. WebSocket and API both build on models.
- WebSocketClient callbacks run on its network thread and may call any
  method, including the destructor. Keep that working when changing
  `src/ws/websocket.cpp`; `tests/test_ws_client.cpp` covers it.
- libwebsockets 4.4+ calls `OPENSSL_cleanup()` when its last TLS context is
  destroyed, which breaks all later TLS in the process. The context that
  `keep_openssl_initialized()` never destroys is deliberate; keep it.
- Public failures use `std::expected<T, Error>`. Preserve typed, non-throwing
  boundaries when validating input or transport state.
- Glaze reads and writes every JSON payload. Glaze reflection needs types
  with linkage, so wire structs go in a named namespace, not an anonymous one.
  `strip_null_members` is the only hand-written scanner, and parsers call it
  only after a first parse fails.
- `PROJECT_VERSION` generates `kalshi::VERSION` and the CMake package version.
- Public headers live in `include/kalshi/`; implementation-only types stay in
  `src/`.

## Conventions

- Use explicit local types. `tools/cpp_auto_audit.py` allows `auto` for
  structured bindings, lambdas, and iterators; anything else needs an
  `// auto-ok: reason` comment.
- `tools/codegen/generate.py` writes the REST client from `spec/openapi.yaml`
  and the WebSocket types from `spec/asyncapi.yaml`. Edit a spec, the
  generator, or a `tools/codegen/*.in` template, then run `make codegen`;
  never edit its output.
- WebSocket commands must keep the key order `id`, `cmd`, `params`;
  `tests/test_ws_subscriptions.cpp` pins it.
- Format with the repository `.clang-format`: tabs, 100 columns, project
  includes before system includes.
