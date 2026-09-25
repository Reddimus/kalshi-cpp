# Development tools

| Tool | Purpose |
| --- | --- |
| `codegen/generate.py` | Generates the REST client and WebSocket types, their tests, and docs from `spec/` |
| `cpp_auto_audit.py` | Enforces the explicit-local-type rule (`make lint` runs it) |
| `project_version.sh` | Prints the version declared in `CMakeLists.txt` |
| `test_consumers.sh` | Builds installed and FetchContent consumers (`make consumers`) |

The generator needs PyYAML (`python3-yaml`, or pip in a virtual environment) and clang-format
18. `make codegen` runs it, and `make lint` fails when its output is stale.
`codegen/api.hpp.in` and `codegen/ws_models.hpp.in` hold the hand-written
parts of the generated headers; `codegen/ws.py` maps the AsyncAPI document.

Benchmarks live in `benchmarks/`. Run them with `make bench`, and pass Google
Benchmark flags through `BENCH_ARGS`, for example
`make bench BENCH_ARGS=--benchmark_filter=WsParse`. Each reports heap
allocations per iteration as `allocs` and `alloc_bytes`, and on macOS the
`instructions` per iteration, which stay steady on a busy machine.
