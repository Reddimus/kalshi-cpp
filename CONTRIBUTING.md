# Contributing to kalshi-cpp

Thanks for your interest in contributing. This guide covers the local
build flow, code style expectations, and PR conventions used in this
repo. For security-vulnerability reports, see [SECURITY.md](SECURITY.md)
and do not open a public issue for those.

## Getting started

```bash
git clone https://github.com/Reddimus/kalshi-cpp.git
cd kalshi-cpp

# One-time: install build deps (Ubuntu 24.04 example)
sudo apt install -y build-essential cmake clang-format \
    libssl-dev libcurl4-openssl-dev libwebsockets-dev

make build      # CMake configure + Release build
make test       # Run unit tests (ctest)
```

macOS uses Homebrew (`brew install openssl curl libwebsockets`),
Windows uses vcpkg (the CI workflow has the exact invocations).

## Development workflow

```bash
make debug          # Debug build in build-debug/
make test           # Build and run the tests
make sanitize       # ASan + UBSan
make tsan           # ThreadSanitizer
make tidy           # clang-tidy build
make lint           # clang-format 18, cpp_auto_audit, generated-code check
make codegen        # Regenerate the REST client from spec/openapi.yaml
make format         # Apply clang-format in place
make bench          # Google Benchmark suite
make coverage       # lcov report (needs lcov)
make clean          # Remove build directories
```

Run `make lint` before pushing; CI runs the same checks. It needs
clang-format 18 and PyYAML (`python3 -m pip install pyyaml`).

## Generated code

`include/kalshi/api.hpp`, `include/kalshi/models.hpp`, `src/api/operations/`,
`src/api/json_meta.hpp`, `src/api/validate.hpp`, `tests/test_operation_routes.cpp`,
and `docs/operations.md` come from `tools/codegen/generate.py`. Change the
generator or `spec/openapi.yaml`, run `make codegen`, and commit both. CI rejects stale
output. `docs/research.md` explains how to refresh the spec.

## Code style

- **C++23** features encouraged: `std::expected<T, Error>` for all
  error-returning operations, no exceptions in the public API.
- **No `auto`** for local variable declarations. Spell out the type so
  reviewers can verify intent without IDE help. Carve-outs:
  - Structured bindings: `auto& [k, v] = ...`
  - Lambda closures: `auto callback = ...`
  - Iterator-like results: `auto it = container.find(...)`
- **Formatting**: `.clang-format` (LLVM base, tabs, 100-col limit).
  `make format` applies it.
- **Includes**: project headers first, then system headers
  (enforced by clang-format `SortIncludes`).
- **JSON**: use Glaze for structured payloads. Keep hand-rolled scanners limited
  to measured hot paths with focused parser and benchmark tests.

## PR conventions

- Branch names: `feat/...`, `fix/...`, `docs/...`, `chore/...`,
  `test/...`, `build/...`, `ci/...`, `refactor/...`.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):
  `<type>(<scope>): <summary>`, for example:
  `fix(ws): null-guard moved-from accessors`.
- Squash + delete branch on merge. PR titles become the squash commit
  subject, so write them clearly.
- Update `CHANGELOG.md` under `## [Unreleased]` for any user-visible
  change (new API, fix that consumers will notice, dep bump). Use the
  Keep-a-Changelog sub-headers: Added / Changed / Fixed / Removed.
- CI must pass on all platforms (Ubuntu 24.04 + macOS + Windows) before
  merge.

## Release process

Releases are cut from `main` via tag push:

```bash
# 1. Update CMakeLists.txt VERSION and move CHANGELOG.md entries into [X.Y.Z].
#    release.yml refuses tags without a matching CHANGELOG section or passing CI.
# 2. Commit the version bump
git commit -am "chore(release): cut vX.Y.Z"
git push origin main

# 3. Tag and push the tag; release.yml creates the GitHub Release
git tag vX.Y.Z
git push origin vX.Y.Z
```

Semver: bump MINOR for new public API, PATCH for fixes/docs/CI.

## Reporting issues

- **Bugs / feature requests**: open a GitHub issue with reproduction
  steps + the kalshi-cpp version (`kalshi::VERSION`).
- **Security vulnerabilities**: see [SECURITY.md](SECURITY.md) for the
  private reporting channel.

## License

By contributing, you agree your changes are licensed under the MIT
license that covers this repository.
