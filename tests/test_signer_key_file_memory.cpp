// Signer::from_pem_file and memory: key bytes must be wiped before the memory
// that held them is freed, and running out of memory must not throw.

#include "kalshi/signer.hpp"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>

#include "support/allocation_counter.hpp"
#include "support/scratch_directory.hpp"
#include "test_signer_fixture.hpp"

namespace {

// Constant-initialized, so the observer can read them from operator delete.
constinit thread_local std::string_view key_text;
constinit thread_local std::uint64_t blocks_freed_with_key_text = 0;

void find_key_text(const void* block, std::size_t size) noexcept {
	const std::string_view contents{static_cast<const char*>(block), size};
	if (contents.find(key_text) != std::string_view::npos) {
		++blocks_freed_with_key_text;
	}
}

TEST(SignerKeyFileMemory, FreedMemoryHoldsNoKeyBytes) {
	const kalshi::test::ScratchDirectory scratch;
	const std::string& pem = kalshi::test::private_key_pem();
	const std::string path = scratch.write("kalshi.pem", pem);
	// A stretch of the base64 body, from the middle of the key.
	key_text = std::string_view{pem}.substr(pem.size() / 2, 48);
	blocks_freed_with_key_text = 0;

	// The observer sees operator delete only: the SDK's read buffer and any
	// C++ stream buffer. OpenSSL's buffers and a C stream's come from malloc.
	kalshi::test::set_free_observer(find_key_text);
	const bool loaded = kalshi::Signer::from_pem_file("key", path).has_value();
	kalshi::test::set_free_observer(nullptr);

	EXPECT_TRUE(loaded);
	EXPECT_EQ(blocks_freed_with_key_text, 0U);
}

TEST(SignerKeyFileMemory, RejectedFilesAreWipedToo) {
	const kalshi::test::ScratchDirectory scratch;
	// Readable, but not a key the SDK accepts.
	const std::string pem = kalshi::test::generate_pem("EC");
	const std::string path = scratch.write("ec.pem", pem);
	key_text = std::string_view{pem}.substr(pem.size() / 2, 48);
	blocks_freed_with_key_text = 0;

	kalshi::test::set_free_observer(find_key_text);
	const bool loaded = kalshi::Signer::from_pem_file("key", path).has_value();
	kalshi::test::set_free_observer(nullptr);

	EXPECT_FALSE(loaded);
	EXPECT_EQ(blocks_freed_with_key_text, 0U);
}

TEST(SignerKeyFileMemory, RunningOutOfMemoryIsAnErrorNotAnException) {
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
	GTEST_SKIP() << "debug iterators allocate for every string, so no Error can be built";
#endif
	const kalshi::test::ScratchDirectory scratch;
	const std::string path = scratch.write("kalshi.pem", kalshi::test::private_key_pem());
	bool threw = false;
	std::optional<kalshi::ErrorCode> code;

	kalshi::test::set_allocation_failure(true);
	try {
		const kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("key", path);
		if (!signer) {
			code = signer.error().code;
		}
	} catch (...) {
		threw = true;
	}
	kalshi::test::set_allocation_failure(false);

	EXPECT_FALSE(threw);
	EXPECT_EQ(code, kalshi::ErrorCode::InvalidKey);
}

} // namespace
