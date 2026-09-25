// The heap allocation counter in support/allocation_counter.hpp. Budget tests
// built on it are only as good as these counts.

#include <gtest/gtest.h>
#include <latch>
#include <new>
#include <string>
#include <thread>

#include "support/allocation_counter.hpp"

namespace {

using kalshi::test::AllocationCount;
using kalshi::test::AllocationProbe;

const void* volatile escaped = nullptr;

// Publishes a pointer so the optimizer cannot remove the allocation behind it.
void escape(const void* pointer) {
	escaped = pointer;
}

TEST(AllocationCounter, CountsOneNew) {
	const AllocationProbe probe;
	int* value = new int(7);
	escape(value);
	delete value;
	const AllocationCount used = probe.count();
	EXPECT_EQ(used.count, 1U);
	EXPECT_EQ(used.bytes, sizeof(int));
}

TEST(AllocationCounter, CountsArrayAndNothrowNew) {
	const AllocationProbe probe;
	int* values = new int[10];
	escape(values);
	delete[] values;
	int* value = new (std::nothrow) int(1);
	escape(value);
	delete value;
	const AllocationCount used = probe.count();
	EXPECT_EQ(used.count, 2U);
	EXPECT_EQ(used.bytes, 11 * sizeof(int));
}

TEST(AllocationCounter, FreeingDoesNotCount) {
	int* value = new int(7);
	escape(value);
	const AllocationProbe probe;
	delete value;
	EXPECT_EQ(probe.count().count, 0U);
}

TEST(AllocationCounter, CountsStandardContainers) {
	const AllocationProbe probe;
	std::string text(100, 'x');
	escape(text.data());
	// Implementations may round the capacity up.
	EXPECT_GE(probe.count().count, 1U);
	EXPECT_GE(probe.count().bytes, 101U);
}

TEST(AllocationCounter, IgnoresOtherThreads) {
	std::latch go(1);
	std::thread worker([&go] {
		go.wait();
		int* value = new int(7);
		escape(value);
		delete value;
	});
	const AllocationProbe probe;
	go.count_down();
	worker.join();
	EXPECT_EQ(probe.count().count, 0U);
}

TEST(AllocationCounter, NestedProbesEachSeeTheirOwnScope) {
	const AllocationProbe outer;
	int* first = new int(1);
	escape(first);
	{
		const AllocationProbe inner;
		int* second = new int(2);
		escape(second);
		delete second;
		EXPECT_EQ(inner.count().count, 1U);
	}
	delete first;
	EXPECT_EQ(outer.count().count, 2U);
}

TEST(AllocationCounter, TracksPeakLiveBytes) {
	const AllocationProbe outer;
	char* big = new char[4000];
	escape(big);
	delete[] big;
	{
		const AllocationProbe inner;
		char* small = new char[100];
		escape(small);
		delete[] small;
		// Allocators round block sizes up, but not by this much.
		EXPECT_GE(inner.peak_live_bytes(), 100U);
		EXPECT_LT(inner.peak_live_bytes(), 1000U);
	}
	// The inner probe must not hide the outer one's earlier peak.
	EXPECT_GE(outer.peak_live_bytes(), 4000U);
	EXPECT_LT(outer.peak_live_bytes(), 8000U);
}

// Peaks are net growth: freeing an older block makes room for newer ones.
TEST(AllocationCounter, PeakIsNetOfOlderBlocks) {
	char* old = new char[4000];
	escape(old);
	const AllocationProbe probe;
	delete[] old;
	char* fresh = new char[1000];
	escape(fresh);
	delete[] fresh;
	EXPECT_EQ(probe.peak_live_bytes(), 0U);
	EXPECT_EQ(probe.count().count, 1U);
}

} // namespace
