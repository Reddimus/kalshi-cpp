#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace kalshi::detail {

/// Thread-safe callback storage that never invokes user code while locked.
template <typename Signature>
class CallbackSlot;

template <typename Return, typename... Args>
class CallbackSlot<Return(Args...)> {
public:
	void set(std::function<Return(Args...)> callback) {
		std::lock_guard lock(mutex_);
		callback_ = std::move(callback);
	}

	void invoke(Args... args) noexcept {
		try {
			std::function<Return(Args...)> callback;
			{
				std::lock_guard lock(mutex_);
				callback = callback_;
			}
			if (callback)
				callback(args...);
		} catch (...) {
			// User callbacks must not unwind through the transport's C callback.
		}
	}

private:
	std::mutex mutex_;
	std::function<Return(Args...)> callback_;
};

/// Join a worker from any thread except that worker itself.
///
/// A false result leaves the thread joinable so its owner can reap it after
/// the callback returns and the service loop exits.
inline bool join_thread_unless_current(std::thread& thread) noexcept {
	if (!thread.joinable())
		return true;
	if (thread.get_id() == std::this_thread::get_id())
		return false;
	thread.join();
	return true;
}

} // namespace kalshi::detail
