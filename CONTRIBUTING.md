# Contributing

Report security problems privately as described in [SECURITY.md](SECURITY.md),
not in a public issue.

## Set up

```bash
git clone https://github.com/Reddimus/kalshi-cpp.git
cd kalshi-cpp
```

On Ubuntu 24.04, whose apt CMake is older than the 3.31 this build needs:

```bash
sudo apt install build-essential ninja-build pkg-config clang-format-18 python3-yaml \
    pipx libssl-dev libcurl4-openssl-dev libwebsockets-dev
pipx install cmake && pipx ensurepath   # then open a new shell
make test
```

On macOS, Homebrew's `llvm@18` provides clang-format and clang-tidy 18, which
`make` finds on its own. PyYAML goes in a virtual environment:

```bash
brew install cmake ninja pkg-config openssl libwebsockets llvm@18
python3 -m venv .venv && .venv/bin/pip install pyyaml
export PYTHON=.venv/bin/python
make test lint
```

Windows builds use vcpkg; the `build-windows` job in `.github/workflows/ci.yml`
has the steps.

## Everyday commands

```bash
make test            # Release build and tests
make debug           # Debug build in build-debug/
make sanitize tsan   # ASan + UBSan, ThreadSanitizer
make tidy            # clang-tidy build
make lint            # clang-format 18, explicit-type audit, generated-code check
make format          # Apply clang-format
make codegen         # Regenerate from spec/
make docs            # Doxygen API reference in build-docs/html (needs Doxygen)
make bench           # Google Benchmark suite
make consumers       # Build installed and FetchContent consumers
make coverage        # lcov report (needs lcov)
make lint-docs       # markdownlint (needs markdownlint-cli2)
```

Run `make format lint test` before pushing, and `make lint-docs` (needs
`markdownlint-cli2`) after editing Markdown. `make install-hooks` runs
`make lint` on every commit. CI builds and tests on Linux, macOS, and Windows,
and runs the sanitizers, clang-tidy, and the consumer check on Linux.

`make tidy` needs clang-tidy 18. On Ubuntu, build with the matching clang and
libc++, as CI does:

```bash
make tidy CMAKE_ARGS="-DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_CXX_FLAGS=-stdlib=libc++"
```

## Generated code

`tools/codegen/generate.py` writes the REST client from `spec/openapi.yaml` and
the WebSocket types from `spec/asyncapi.yaml`: `include/kalshi/api.hpp`,
`models.hpp`, and `ws_models.hpp`, `src/api/operations/`,
`src/api/validate.hpp`, `src/models/json_meta.hpp`, `src/ws/wire.hpp`,
`tests/test_operation_routes.cpp`, `tests/test_ws_messages.cpp`,
`docs/operations.md`, and `docs/channels.md`. Change the generator, a template
in `tools/codegen/`, or a spec, run `make codegen`, and commit the result.
`make lint` fails on stale output. [docs/research.md](docs/research.md)
explains how to refresh the specs.

## Code style

- Public functions return `kalshi::Result<T>` (`std::expected<T, Error>`) and
  don't throw.
- Spell out local variable types. `auto` is fine for structured bindings,
  lambdas, and iterators. Anything else needs an `// auto-ok: reason` comment
  or an entry in `tools/cpp_auto_allowlist.txt`; `make lint` checks this.
- `.clang-format` sets the layout: tabs, 100 columns, project includes before
  system includes.
- Glaze reads and writes JSON. `strip_null_members` is the only hand-written
  scanner, and it runs only after a parse fails.
- Keep tests offline. Inject an `HttpTransport`, or use the local HTTP and
  WebSocket servers in `tests/`.

## Pull requests

- Name branches `feat/`, `fix/`, `perf/`, `docs/`, `ci/`, `refactor/`, `test/`,
  or `chore/`.
- Title PRs as [Conventional Commits](https://www.conventionalcommits.org/),
  such as `fix(ws): keep subscriptions across reconnects`. PRs are
  squash-merged, so the title becomes the commit subject.
- Note user-visible changes in `CHANGELOG.md` under `[Unreleased]`.

## Releases

1. Set `VERSION` in `CMakeLists.txt`, move the `[Unreleased]` notes into a new
   `[X.Y.Z]` section, update the compare links at the bottom of
   `CHANGELOG.md`, and update the `GIT_TAG` in `README.md`. Release notes
   come from that section, so link with absolute URLs.
2. Merge that change to `main`.
3. Run `git tag vX.Y.Z && git push origin vX.Y.Z`. `release.yml` publishes the
   GitHub release after CI passes on the tagged commit, and `docs.yml`
   publishes the API reference.

While the version is 0.x, a minor release may break the API; patch releases
only fix things.

## License

Contributions are licensed under the repository's MIT license.
