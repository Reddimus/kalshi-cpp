#pragma once

// Counts heap allocations by replacing global operator new and delete, so
// tests and benchmarks can check how much a call allocates. CMake builds it as
// the kalshi_allocation_counter object library, because a replacement in a
// static library might never be linked. Sanitizer and coverage builds leave it
// out; ASan and TSan replace operator new themselves.
//
// Only operator new is counted. OpenSSL and libcurl call malloc directly, and
// over-aligned allocations, which take std::align_val_t, keep the default
// operators.

#include <cstdint>

namespace kalshi::test {

/// Allocations made through global operator new.
struct AllocationCount {
	std::uint64_t count{0};
	/// Bytes requested, before the allocator rounds them up.
	std::uint64_t bytes{0};

	friend AllocationCount operator-(AllocationCount lhs, AllocationCount rhs) noexcept {
		return {lhs.count - rhs.count, lhs.bytes - rhs.bytes};
	}
};

/// The calling thread's allocations since it started. Counters are per thread,
/// so other threads' work never shows up in a measurement. Benchmarks subtract
/// two snapshots instead of opening a probe.
[[nodiscard]] AllocationCount allocations() noexcept;

/// Measures the calling thread's allocations from construction on, including
/// peak live bytes. While a probe is open, every allocation and free also
/// looks up the block's size, so benchmarks use allocations() instead. Probes
/// can nest; keep each on the thread that created it.
class AllocationProbe {
public:
	AllocationProbe() noexcept;
	~AllocationProbe();
	AllocationProbe(const AllocationProbe&) = delete;
	AllocationProbe& operator=(const AllocationProbe&) = delete;

	/// Allocations since construction.
	[[nodiscard]] AllocationCount count() const noexcept;

	/// Most heap bytes live at once since construction, measured from the level
	/// at construction. Freeing an older block lowers that level, so this is net
	/// growth, not the sum of new blocks. Sizes come from the allocator, which
	/// rounds requests up.
	[[nodiscard]] std::uint64_t peak_live_bytes() const noexcept;

private:
	AllocationCount start_;
	std::int64_t start_live_{0};
	std::int64_t outer_peak_{0};
};

} // namespace kalshi::test
