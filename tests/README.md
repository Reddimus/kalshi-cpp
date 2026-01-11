# Tests Directory

Unit tests for the Kalshi C++ SDK.

## Test Files

| File | Tests |
| ---- | ----- |
| `test_main.cpp` | Test framework and runner |
| `test_signer.cpp` | RSA-PSS signing tests |
| `test_models.cpp` | Data model tests |

## Running Tests

From this directory:

```bash
make test
```

Or from the repository root:

```bash
make test
```

## Test Framework

Uses a minimal built-in test framework with macros:

- `TEST(name)` - Define a test
- `ASSERT_TRUE(expr)` - Assert expression is true
- `ASSERT_FALSE(expr)` - Assert expression is false
- `ASSERT_EQ(a, b)` - Assert equality
- `ASSERT_NE(a, b)` - Assert inequality

## Adding Tests

1. Create a new `test_*.cpp` file
2. Include the test macros (see existing files)
3. Add to `tests/CMakeLists.txt`
4. Run `make test`
