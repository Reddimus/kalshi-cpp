# Development tools

| Tool | Purpose |
| --- | --- |
| `bench.sh` | Compare executable timing and binary sizes |
| `cpp_auto_audit.py` | Enforce the repository's explicit-local-type rule |
| `test_consumers.sh` | Test installed and FetchContent CMake consumers |

Run a benchmark against the current `build/` directory:

```bash
./tools/bench.sh 100
```

Compare the working tree with `HEAD`, or compare two committed refs:

```bash
./tools/bench.sh --compare 100
./tools/bench.sh --compare v0.5.1 v0.5.2 100
```

Comparison mode builds detached temporary worktrees and removes them on exit. It
does not stash, switch, or modify the active checkout.
