# Tools Directory

Build and development tools for the Kalshi C++ SDK.

## Contents

### bench.sh

A unified benchmark tool for timing and size analysis. Supports both single-build benchmarking and comparison between git refs.

**Features:**

- **Single mode**: Benchmark current build with timing statistics
- **Compare mode**: Build and compare two git refs (commits, branches, HEAD vs working tree)
- Library and executable size reporting
- Color-coded deltas (green = improvement, red = regression)
- Statistical metrics: Min, P50, Mean, P95, Max

**Usage:**

```bash
# Single build benchmark (default: 100 iterations)
make bench
./tools/bench.sh [iterations]

# Compare HEAD vs working tree
make bench-compare  
./tools/bench.sh --compare [iterations]

# Compare specific refs
./tools/bench.sh --compare main feature
./tools/bench.sh --compare abc123 def456 100
```

**Example output (compare mode):**

```text
=== Size Comparison ===

--- Static Libraries ---
Library                   Old             New                Delta
libkalshi_http.a     35.74 KB        30.43 KB    -5.31 KB (-14%)
libkalshi_core.a     29.78 KB        26.38 KB    -3.40 KB (-11%)
TOTAL               398.15 KB       387.59 KB   -10.55 KB (-2%)

=== Timing Comparison (ms) ===

--- kalshi_tests ---
Metric      Old        New        Delta
min         5ms        5ms        --
mean        6ms        6ms        --
p95         8ms        8ms        --
```

**Metrics explained:**

- **Min** - Fastest run (best case)
- **P50** - Median (50% of runs were faster)
- **Mean** - Average time across all runs  
- **P95** - 95th percentile (good for latency budgets)
- **Max** - Slowest run (worst case)

## Future Tools

This directory will contain:

- Code generators
- Development scripts
- Build utilities
