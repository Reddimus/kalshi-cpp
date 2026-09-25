#include "allocation_counter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

namespace kalshi::test {
namespace {

struct Counters {
	std::uint64_t count{0};
	std::uint64_t bytes{0};
	// Live and peak bytes change only while a probe is open, because looking up
	// a block's size costs about as much as malloc on macOS.
	int probes{0};
	// Signed, because freeing a block from before the probe opened lowers it.
	std::int64_t live{0};
	std::int64_t peak{0};
};

// Constant-initialized and trivially destructible, so it is safe to use from
// operator new at any point in a thread's life.
constinit thread_local Counters counters;

std::int64_t block_size(void* block) noexcept {
#if defined(__APPLE__)
	return static_cast<std::int64_t>(malloc_size(block));
#elif defined(_WIN32)
	return static_cast<std::int64_t>(_msize(block));
#else
	return static_cast<std::int64_t>(malloc_usable_size(block));
#endif
}

void* allocate(std::size_t size) noexcept {
	void* block = std::malloc(size == 0 ? 1 : size);
	if (block != nullptr) {
		Counters& c = counters;
		++c.count;
		c.bytes += size;
		if (c.probes > 0) {
			c.live += block_size(block);
			c.peak = std::max(c.peak, c.live);
		}
	}
	return block;
}

void release(void* block) noexcept {
	if (block != nullptr) {
		Counters& c = counters;
		if (c.probes > 0) {
			c.live -= block_size(block);
		}
		std::free(block);
	}
}

} // namespace

AllocationCount allocations() noexcept {
	return {counters.count, counters.bytes};
}

AllocationProbe::AllocationProbe() noexcept
	: start_(allocations()), start_live_(counters.live), outer_peak_(counters.peak) {
	++counters.probes;
	counters.peak = counters.live;
}

// Hands an enclosing probe the larger of the two peaks.
AllocationProbe::~AllocationProbe() {
	counters.peak = std::max(counters.peak, outer_peak_);
	--counters.probes;
}

AllocationCount AllocationProbe::count() const noexcept {
	return allocations() - start_;
}

std::uint64_t AllocationProbe::peak_live_bytes() const noexcept {
	return static_cast<std::uint64_t>(std::max<std::int64_t>(counters.peak - start_live_, 0));
}

} // namespace kalshi::test

// The replacements. Array, nothrow, and sized forms share one path, so each
// allocation counts once.

void* operator new(std::size_t size) {
	// A standard operator new retries through the new-handler, then throws.
	for (;;) {
		if (void* block = kalshi::test::allocate(size)) {
			return block;
		}
		const std::new_handler handler = std::get_new_handler();
		if (handler == nullptr) {
			throw std::bad_alloc();
		}
		handler();
	}
}

void* operator new[](std::size_t size) {
	return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	try {
		return ::operator new(size);
	} catch (...) {
		return nullptr;
	}
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	return ::operator new(size, std::nothrow);
}

void operator delete(void* block) noexcept {
	kalshi::test::release(block);
}

void operator delete[](void* block) noexcept {
	kalshi::test::release(block);
}

void operator delete(void* block, std::size_t) noexcept {
	kalshi::test::release(block);
}

void operator delete[](void* block, std::size_t) noexcept {
	kalshi::test::release(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept {
	kalshi::test::release(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept {
	kalshi::test::release(block);
}
