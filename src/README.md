# Source Directory

This directory contains the implementation files for the Kalshi C++ SDK.

## Structure

```text
src/
├── core/       # Core utilities (error handling, rate limiting, retry)
├── auth/       # RSA-PSS authentication
├── http/       # HTTP client
├── ws/         # WebSocket streaming
└── models/     # Data model implementations
```

## Build Targets

| Target | Description |
| ------ | ----------- |
| `kalshi_core` | Error handling, rate limiting, retry logic |
| `kalshi_auth` | Request signing with RSA-PSS |
| `kalshi_http` | HTTP client with authentication |
| `kalshi_ws` | WebSocket streaming client |
| `kalshi_models` | Market, Order, Trade models |

## Building

From this directory:

```bash
make build    # Build all targets
make test     # Run tests
make clean    # Clean build artifacts
```

Or from the repository root:

```bash
make build
```
