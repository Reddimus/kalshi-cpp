#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace kalshi::detail {

/// Thread-safe callback storage that never calls user code while locked.
/// Invoking copies a shared_ptr rather than the std::function, so a frame
/// costs no allocation.
template <typename Signature>
class CallbackSlot;

template <typename Return, typename... Args>
class CallbackSlot<Return(Args...)> {
public:
	void set(std::function<Return(Args...)> callback) {
		std::shared_ptr<const std::function<Return(Args...)>> next =
			callback ? std::make_shared<std::function<Return(Args...)>>(std::move(callback))
					 : nullptr;
		const std::lock_guard lock(mutex_);
		callback_ = std::move(next);
	}

	void invoke(Args... args) const noexcept {
		std::shared_ptr<const std::function<Return(Args...)>> callback;
		{
			const std::lock_guard lock(mutex_);
			callback = callback_;
		}
		if (!callback) {
			return;
		}
		try {
			(*callback)(args...);
		} catch (...) { // NOLINT(bugprone-empty-catch): must not unwind into C callbacks
		}
	}

private:
	mutable std::mutex mutex_;
	std::shared_ptr<const std::function<Return(Args...)>> callback_;
};

} // namespace kalshi::detail
