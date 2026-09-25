# Development tools

| Tool | Purpose |
| --- | --- |
| `cpp_auto_audit.py` | Enforce the explicit-local-type rule (`make lint` runs it) |
| `project_version.sh` | Print the version declared in `CMakeLists.txt` |
| `test_consumers.sh` | Build installed and FetchContent consumers (`make consumers`) |

Benchmarks live in `benchmarks/`. Run them with `make bench`, and pass Google
Benchmark flags through `BENCH_ARGS`, for example
`make bench BENCH_ARGS=--benchmark_filter=WsParse`.
