# Examples Directory

Example programs demonstrating Kalshi C++ SDK usage.

## Examples

| File                     | Description                                    |
| ------------------------ | ---------------------------------------------- |
| `basic_usage.cpp`        | Basic API setup and request                    |
| `get_markets.cpp`        | Fetch markets from the API                     |
| `get_daily_high_temp.cpp`| Get highest temperature prediction for cities  |

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
# Using make targets
make run-basic_usage
make run-get_markets
make run-get_daily_high_temp

# Or directly
./build/examples/example_basic
./build/examples/example_markets
./build/examples/example_daily_high_temp
```

## Getting API Keys

1. Go to [Kalshi API Settings](https://kalshi.com/settings/api)
2. Generate an API key pair
3. Download the private key PEM file
4. Note the key ID shown on the page
