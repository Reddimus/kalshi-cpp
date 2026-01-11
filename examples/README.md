# Examples Directory

Example programs demonstrating Kalshi C++ SDK usage.

## Examples

| File               | Description                    |
| ------------------ | ------------------------------ |
| `basic_usage.cpp`  | Basic API setup and request    |
| `get_markets.cpp`  | Fetch markets from the API     |

## Building Examples

From this directory:

```bash
make build
```

## Running Examples

Examples require API credentials. Set environment variables:

```bash
export KALSHI_API_KEY_ID="your-api-key-id"
export KALSHI_API_KEY_FILE="/path/to/your/private-key.pem"
```

Then run:

```bash
./build/examples/example_basic
./build/examples/example_markets
```

## Getting API Keys

1. Go to [Kalshi API Settings](https://kalshi.com/settings/api)
2. Generate an API key pair
3. Download the private key PEM file
4. Note the key ID shown on the page
