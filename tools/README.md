# Development tools

| Tool | Purpose |
| --- | --- |
| `codegen/generate.py` | Generates the REST client, models, route tests, and `docs/operations.md` from `spec/openapi.yaml` |
| `cpp_auto_audit.py` | Enforces the explicit-local-type rule (`make lint` runs it) |
| `project_version.sh` | Prints the version declared in `CMakeLists.txt` |
| `test_consumers.sh` | Builds installed and FetchContent consumers (`make consumers`) |

The generator needs PyYAML (`python3 -m pip install pyyaml`) and clang-format
18. `make codegen` runs it, and `make lint` fails when its output is stale.
`codegen/api.hpp.in` holds the hand-written parts of `KalshiClient`.

Benchmarks live in `benchmarks/`. Run them with `make bench`, and pass Google
Benchmark flags through `BENCH_ARGS`, for example
`make bench BENCH_ARGS=--benchmark_filter=WsParse`.
