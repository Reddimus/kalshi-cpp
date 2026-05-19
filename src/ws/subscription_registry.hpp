#pragma once

#include <cstdint>
#include <map>
#include <mutex>

namespace kalshi::ws_detail {

class SubscriptionRegistry {
public:
	void register_ack(std::int32_t client_id, std::int32_t server_sid) {
		std::lock_guard lock(mutex_);
		client_to_server_sid_[client_id] = server_sid;
	}

	[[nodiscard]] std::int32_t resolve(std::int32_t client_id) const {
		std::lock_guard lock(mutex_);
		auto it = client_to_server_sid_.find(client_id);
		return it == client_to_server_sid_.end() ? client_id : it->second;
	}

	void erase(std::int32_t client_id) {
		std::lock_guard lock(mutex_);
		client_to_server_sid_.erase(client_id);
	}

	void clear() {
		std::lock_guard lock(mutex_);
		client_to_server_sid_.clear();
	}

private:
	mutable std::mutex mutex_;
	std::map<std::int32_t, std::int32_t> client_to_server_sid_;
};

} // namespace kalshi::ws_detail
