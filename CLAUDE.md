# kalshi-cpp development guide

## Required gates

Run `make test`, `make lint`, and `./tools/test_consumers.sh` before review.
User-visible changes also require a `CHANGELOG.md` entry. The CI workflow is
the source of truth for Linux, macOS, Windows, sanitizer, clang-tidy, Markdown,
and consumer gates.

Examples make authenticated network calls. Keep automated tests offline by
using an injected `HttpTransport`.

## Architecture

- Targets are layered as core, auth, HTTP, models, WebSocket, API, then the
  `kalshi` interface target.
- Public failures use `std::expected<T, Error>`. Preserve typed, non-throwing
  boundaries when validating input or transport state.
- Glaze serializes structured payloads. Focused scanners are reserved for
  measured hot paths and require parser and benchmark coverage.
- `PROJECT_VERSION` generates `kalshi::VERSION` and the CMake package version.
- Public headers live in `include/kalshi/`; implementation-only types stay in
  `src/`.

## Conventions

- Use explicit local types. The permitted `auto` cases are recorded in
  `tools/cpp_auto_allowlist.txt` and enforced by `tools/cpp_auto_audit.py`.
- Scope focused JSON scanners to the relevant object before reading repeated
  keys.
- Keep outgoing wire keys in `src/api/json_bodies.hpp` and
  `src/ws/ws_cmd_bodies.hpp`; their order is contract-tested.
- Format with the repository `.clang-format`: tabs, 100 columns, project
  includes before system includes.
