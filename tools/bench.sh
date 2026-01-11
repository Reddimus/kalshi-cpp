#!/usr/bin/env bash
# bench.sh - Benchmark tool for timing and size analysis
#
# Usage:
#   ./tools/bench.sh [options] [iterations]
#
# Modes:
#   ./tools/bench.sh [iterations]              Run benchmarks on current build (default: 100)
#   ./tools/bench.sh --compare [iterations]    Compare HEAD vs working tree
#   ./tools/bench.sh --compare old new [iter]  Compare two git refs
#
# Options:
#   -c, --compare     Enable comparison mode
#   -h, --help        Show this help
#
# Examples:
#   ./tools/bench.sh                           # Benchmark current build (100 iterations)
#   ./tools/bench.sh 50                        # Benchmark with 50 iterations
#   ./tools/bench.sh --compare                 # Compare HEAD vs working tree
#   ./tools/bench.sh --compare 50              # Compare with 50 iterations
#   ./tools/bench.sh --compare main feature    # Compare two branches
#   ./tools/bench.sh --compare abc123 def456 100  # Compare commits with 100 iterations

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Defaults
COMPARE_MODE=false
ITERATIONS=100
OLD_REF=""
NEW_REF=""
BUILD_DIR="${PROJECT_DIR}/build"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--compare)
            COMPARE_MODE=true
            shift
            ;;
        -h|--help)
            head -25 "$0" | tail -22
            exit 0
            ;;
        *)
            if [[ "$COMPARE_MODE" == "true" ]]; then
                # In compare mode: could be old_ref, new_ref, or iterations
                if [[ -z "$OLD_REF" && ! "$1" =~ ^[0-9]+$ ]]; then
                    OLD_REF="$1"
                elif [[ -n "$OLD_REF" && -z "$NEW_REF" && ! "$1" =~ ^[0-9]+$ ]]; then
                    NEW_REF="$1"
                elif [[ "$1" =~ ^[0-9]+$ ]]; then
                    ITERATIONS="$1"
                fi
            else
                # In normal mode: just iterations
                if [[ "$1" =~ ^[0-9]+$ ]]; then
                    ITERATIONS="$1"
                fi
            fi
            shift
            ;;
    esac
done

# Validate iterations
if [[ "$ITERATIONS" -lt 1 ]]; then
    echo "Error: iterations must be a positive integer" >&2
    exit 1
fi

#------------------------------------------------------------------------------
# Utility functions
#------------------------------------------------------------------------------

format_bytes() {
    local bytes=$1
    local negative=""
    if (( bytes < 0 )); then
        negative="-"
        bytes=$(( -bytes ))
    fi
    if (( bytes >= 1048576 )); then
        local mb=$((bytes / 1048576))
        local remainder=$(( (bytes % 1048576) * 100 / 1048576 ))
        printf "%s%d.%02d MB" "$negative" "$mb" "$remainder"
    elif (( bytes >= 1024 )); then
        local kb=$((bytes / 1024))
        local remainder=$(( (bytes % 1024) * 100 / 1024 ))
        printf "%s%d.%02d KB" "$negative" "$kb" "$remainder"
    else
        printf "%s%d B" "$negative" "$bytes"
    fi
}

get_file_size() {
    stat -c%s "$1" 2>/dev/null || stat -f%z "$1" 2>/dev/null || echo 0
}

find_libraries() {
    local dir="$1"
    find "${dir}/src" -name "*.a" 2>/dev/null | sort || true
}

find_executables() {
    local dir="$1"
    local -a exes=()
    for exe in "${dir}/tests/kalshi_tests" "${dir}/examples/example_basic" "${dir}/examples/example_markets"; do
        [[ -x "$exe" ]] && exes+=("$exe")
    done
    printf '%s\n' "${exes[@]}"
}

# Benchmark a single executable, output: name min p50 mean p95 max size
benchmark_one() {
    local exe="$1"
    local iters="$2"
    local name
    name=$(basename "$exe")
    
    echo -e "  Benchmarking: $name ($iters iterations)..." >&2
    
    local -a times=()
    for ((i = 1; i <= iters; i++)); do
        local start_ns end_ns elapsed_ms
        start_ns=$(date +%s%N)
        "$exe" > /dev/null 2>&1 || true
        end_ns=$(date +%s%N)
        elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
        times+=("$elapsed_ms")
        
        (( i % 50 == 0 )) && echo -e "    Progress: $i/$iters" >&2
    done
    
    IFS=$'\n' sorted=($(sort -n <<<"${times[*]}")); unset IFS
    
    local sum=0
    for t in "${times[@]}"; do sum=$((sum + t)); done
    
    local mean=$((sum / iters))
    local min="${sorted[0]}"
    local max="${sorted[$((iters - 1))]}"
    local p50="${sorted[$((iters / 2))]}"
    local p95_idx=$(( (iters * 95) / 100 ))
    (( p95_idx >= iters )) && p95_idx=$((iters - 1))
    local p95="${sorted[$p95_idx]}"
    local size
    size=$(get_file_size "$exe")
    
    echo "$name $min $p50 $mean $p95 $max $size"
}

#------------------------------------------------------------------------------
# Single build benchmark mode
#------------------------------------------------------------------------------

run_single_benchmark() {
    local build_dir="$1"
    
    if [[ ! -d "$build_dir" ]]; then
        echo "Error: Build directory not found: $build_dir" >&2
        echo "Run 'make build' first." >&2
        exit 1
    fi
    
    echo ""
    echo -e "${BLUE}=== Benchmark Results ===${NC}"
    echo "Build directory: $build_dir"
    echo "Iterations: $ITERATIONS"
    echo ""
    
    # Library sizes
    echo "--- Static Library Sizes ---"
    printf "%-30s %12s\n" "Library" "Size"
    printf "%-30s %12s\n" "-------" "----"
    
    local total_size=0
    while IFS= read -r lib; do
        [[ -z "$lib" ]] && continue
        local size
        size=$(get_file_size "$lib")
        total_size=$((total_size + size))
        printf "%-30s %12s\n" "$(basename "$lib")" "$(format_bytes "$size")"
    done < <(find_libraries "$build_dir")
    
    printf "%-30s %12s\n" "TOTAL" "$(format_bytes "$total_size")"
    echo ""
    
    # Executable benchmarks
    echo "--- Executable Timing (ms) ---"
    printf "%-20s %10s %6s %6s %6s %6s %6s\n" "Executable" "Size" "Min" "P50" "Mean" "P95" "Max"
    printf "%-20s %10s %6s %6s %6s %6s %6s\n" "----------" "----" "---" "---" "----" "---" "---"
    
    while IFS= read -r exe; do
        [[ -z "$exe" ]] && continue
        read -r name min p50 mean p95 max size < <(benchmark_one "$exe" "$ITERATIONS")
        printf "%-20s %10s %6d %6d %6d %6d %6d\n" "$name" "$(format_bytes "$size")" "$min" "$p50" "$mean" "$p95" "$max"
    done < <(find_executables "$build_dir")
    
    echo ""
    echo "Legend: Min=fastest, P50=median, Mean=average, P95=95th percentile, Max=slowest"
}

#------------------------------------------------------------------------------
# Comparison mode
#------------------------------------------------------------------------------

run_comparison() {
    local build_old="${PROJECT_DIR}/build_old"
    local build_new="${PROJECT_DIR}/build_new"
    
    cd "$PROJECT_DIR"
    
    # Check for uncommitted changes
    local has_changes=false
    if ! git diff --quiet HEAD 2>/dev/null || ! git diff --cached --quiet HEAD 2>/dev/null; then
        has_changes=true
    fi
    
    # Determine refs
    if [[ -z "$OLD_REF" ]]; then
        if [[ "$has_changes" == "true" ]]; then
            echo -e "${YELLOW}Uncommitted changes detected. Comparing HEAD vs working tree.${NC}"
            OLD_REF="HEAD"
            NEW_REF="WORKTREE"
        else
            echo -e "${YELLOW}No uncommitted changes. Comparing HEAD~1 vs HEAD.${NC}"
            OLD_REF="HEAD~1"
            NEW_REF="HEAD"
        fi
    fi
    [[ -z "$NEW_REF" ]] && NEW_REF="HEAD"
    
    echo -e "${BLUE}=== Benchmark Comparison ===${NC}"
    echo "Old: $OLD_REF"
    echo "New: $NEW_REF"
    echo "Iterations: $ITERATIONS"
    echo ""
    
    # Save state for cleanup (use global-ish variables for trap access)
    local original_ref
    original_ref=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || git rev-parse HEAD)
    _BENCH_DID_STASH=false
    _BENCH_ORIGINAL_REF="$original_ref"
    _BENCH_BUILD_OLD="$build_old"
    _BENCH_BUILD_NEW="$build_new"
    
    cleanup() {
        echo -e "\n${BLUE}Cleaning up...${NC}"
        [[ "${_BENCH_DID_STASH:-false}" == "true" ]] && git stash pop --quiet 2>/dev/null || true
        [[ -n "${_BENCH_ORIGINAL_REF:-}" ]] && git checkout --quiet "$_BENCH_ORIGINAL_REF" 2>/dev/null || true
        rm -rf "${_BENCH_BUILD_OLD:-}" "${_BENCH_BUILD_NEW:-}"
    }
    trap cleanup EXIT
    
    # Build old version
    echo -e "${BLUE}Building OLD ($OLD_REF)...${NC}"
    if [[ "$OLD_REF" != "WORKTREE" ]]; then
        if [[ "$has_changes" == "true" ]]; then
            git stash push --quiet -m "bench temp stash"
            _BENCH_DID_STASH=true
        fi
        git checkout --quiet "$OLD_REF"
    fi
    rm -rf "$build_old"
    cmake -S . -B "$build_old" -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    cmake --build "$build_old" -j"$(nproc 2>/dev/null || echo 4)" > /dev/null 2>&1
    echo -e "${GREEN}  Done${NC}"
    
    # Build new version
    echo -e "${BLUE}Building NEW ($NEW_REF)...${NC}"
    if [[ "$NEW_REF" == "WORKTREE" ]]; then
        [[ "$_BENCH_DID_STASH" == "true" ]] && { git stash pop --quiet; _BENCH_DID_STASH=false; }
    else
        git checkout --quiet "$NEW_REF"
    fi
    rm -rf "$build_new"
    cmake -S . -B "$build_new" -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    cmake --build "$build_new" -j"$(nproc 2>/dev/null || echo 4)" > /dev/null 2>&1
    echo -e "${GREEN}  Done${NC}"
    echo ""
    
    # Collect data
    declare -A old_lib_sizes new_lib_sizes old_exe_sizes new_exe_sizes
    declare -A old_times new_times
    
    # Library sizes
    while IFS= read -r lib; do
        [[ -z "$lib" ]] && continue
        local name size
        name=$(basename "$lib")
        size=$(get_file_size "$lib")
        old_lib_sizes["$name"]=$size
    done < <(find_libraries "$build_old")
    
    while IFS= read -r lib; do
        [[ -z "$lib" ]] && continue
        local name size
        name=$(basename "$lib")
        size=$(get_file_size "$lib")
        new_lib_sizes["$name"]=$size
    done < <(find_libraries "$build_new")
    
    # Benchmark old
    echo -e "${BLUE}Benchmarking OLD...${NC}"
    while IFS= read -r exe; do
        [[ -z "$exe" ]] && continue
        read -r name min p50 mean p95 max size < <(benchmark_one "$exe" "$ITERATIONS")
        old_exe_sizes["$name"]=$size
        old_times["${name}_min"]=$min
        old_times["${name}_p50"]=$p50
        old_times["${name}_mean"]=$mean
        old_times["${name}_p95"]=$p95
        old_times["${name}_max"]=$max
    done < <(find_executables "$build_old")
    
    # Benchmark new
    echo -e "${BLUE}Benchmarking NEW...${NC}"
    while IFS= read -r exe; do
        [[ -z "$exe" ]] && continue
        read -r name min p50 mean p95 max size < <(benchmark_one "$exe" "$ITERATIONS")
        new_exe_sizes["$name"]=$size
        new_times["${name}_min"]=$min
        new_times["${name}_p50"]=$p50
        new_times["${name}_mean"]=$mean
        new_times["${name}_p95"]=$p95
        new_times["${name}_max"]=$max
    done < <(find_executables "$build_new")
    
    # Print size comparison
    echo ""
    echo -e "${BLUE}=== Size Comparison ===${NC}"
    echo ""
    echo "--- Static Libraries ---"
    printf "%-25s %12s %12s %18s\n" "Library" "Old" "New" "Delta"
    printf "%-25s %12s %12s %18s\n" "-------" "---" "---" "-----"
    
    local total_old=0 total_new=0
    for name in "${!old_lib_sizes[@]}"; do
        local old_val=${old_lib_sizes[$name]:-0}
        local new_val=${new_lib_sizes[$name]:-0}
        total_old=$((total_old + old_val))
        total_new=$((total_new + new_val))
        
        local delta=$((new_val - old_val))
        local pct=0
        (( old_val > 0 )) && pct=$(( delta * 100 / old_val ))
        
        local delta_str
        if (( delta > 0 )); then
            delta_str="${RED}+$(format_bytes $delta) (+${pct}%)${NC}"
        elif (( delta < 0 )); then
            delta_str="${GREEN}$(format_bytes $delta) (${pct}%)${NC}"
        else
            delta_str="--"
        fi
        
        printf "%-25s %12s %12s " "$name" "$(format_bytes "$old_val")" "$(format_bytes "$new_val")"
        echo -e "$delta_str"
    done | sort
    
    local delta=$((total_new - total_old))
    local pct=0
    (( total_old > 0 )) && pct=$(( delta * 100 / total_old ))
    local delta_str
    if (( delta > 0 )); then
        delta_str="${RED}+$(format_bytes $delta) (+${pct}%)${NC}"
    elif (( delta < 0 )); then
        delta_str="${GREEN}$(format_bytes $delta) (${pct}%)${NC}"
    else
        delta_str="--"
    fi
    printf "%-25s %12s %12s " "TOTAL" "$(format_bytes "$total_old")" "$(format_bytes "$total_new")"
    echo -e "$delta_str"
    
    echo ""
    echo "--- Executables ---"
    printf "%-25s %12s %12s %18s\n" "Executable" "Old" "New" "Delta"
    printf "%-25s %12s %12s %18s\n" "----------" "---" "---" "-----"
    
    for name in "${!old_exe_sizes[@]}"; do
        local old_val=${old_exe_sizes[$name]:-0}
        local new_val=${new_exe_sizes[$name]:-0}
        
        local delta=$((new_val - old_val))
        local pct=0
        (( old_val > 0 )) && pct=$(( delta * 100 / old_val ))
        
        local delta_str
        if (( delta > 0 )); then
            delta_str="${RED}+$(format_bytes $delta) (+${pct}%)${NC}"
        elif (( delta < 0 )); then
            delta_str="${GREEN}$(format_bytes $delta) (${pct}%)${NC}"
        else
            delta_str="--"
        fi
        
        printf "%-25s %12s %12s " "$name" "$(format_bytes "$old_val")" "$(format_bytes "$new_val")"
        echo -e "$delta_str"
    done | sort
    
    # Print timing comparison
    echo ""
    echo -e "${BLUE}=== Timing Comparison (ms) ===${NC}"
    
    for name in "${!old_exe_sizes[@]}"; do
        echo ""
        echo "--- $name ---"
        printf "%-8s %8s %8s %18s\n" "Metric" "Old" "New" "Delta"
        printf "%-8s %8s %8s %18s\n" "------" "---" "---" "-----"
        
        for metric in min p50 mean p95 max; do
            local old_val=${old_times["${name}_${metric}"]:-0}
            local new_val=${new_times["${name}_${metric}"]:-0}
            
            local delta=$((new_val - old_val))
            local pct=0
            (( old_val > 0 )) && pct=$(( delta * 100 / old_val ))
            
            local delta_str
            if (( delta > 0 )); then
                delta_str="${RED}+${delta}ms (+${pct}%)${NC}"
            elif (( delta < 0 )); then
                delta_str="${GREEN}${delta}ms (${pct}%)${NC}"
            else
                delta_str="--"
            fi
            
            printf "%-8s %8s %8s " "$metric" "${old_val}ms" "${new_val}ms"
            echo -e "$delta_str"
        done
    done
    
    echo ""
    echo -e "${GREEN}Done!${NC}"
}

#------------------------------------------------------------------------------
# Main
#------------------------------------------------------------------------------

if [[ "$COMPARE_MODE" == "true" ]]; then
    run_comparison
else
    run_single_benchmark "$BUILD_DIR"
fi
