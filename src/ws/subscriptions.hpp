#pragma once

#include "kalshi/websocket.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frames.hpp"

namespace kalshi::detail {

/// What a server frame means for the caller: commands to send in response, and
/// what to report.
struct Reaction {
	std::vector<std::string> frames;
	std::optional<WsMessage> message;
	std::optional<WsError> error;
};

/// Tracks subscriptions and in-flight commands across reconnects.
///
/// A subscription keeps its handle while its server `sid` changes. Commands
/// for a subscription the server has not confirmed yet wait for the
/// confirmation, so they never name a stale or foreign `sid`. Not thread-safe;
/// WebSocketClient guards it with its mutex.
class SubscriptionRegistry {
public:
	/// Adds a subscription. Frames to send are appended only when connected.
	ws::Subscription subscribe(ws::Channel channel, ws::SubscribeParams params, bool connected,
							   std::vector<std::string>& frames);
	Result<void> unsubscribe(ws::Subscription subscription, std::vector<std::string>& frames);
	/// Changes a subscription. The change reaches the parameters used to
	/// resubscribe once the server confirms it, so a rejected change is dropped.
	Result<void> update(ws::Subscription subscription, const ws::UpdateSubscriptionParams& params,
						bool connected, std::vector<std::string>& frames);
	/// Returns the command's ID, which the reply's SubscriptionList carries.
	std::int64_t list_subscriptions(std::vector<std::string>& frames);

	/// Subscribes everything again on a new connection, except subscriptions
	/// whose market filter is now empty.
	void on_connected(std::vector<std::string>& frames);
	/// Forgets the server's state; subscriptions resend on the next connection.
	/// Changes still awaiting confirmation are kept, since their outcome is unknown.
	void on_disconnected();
	void clear();

	Reaction on_subscribed(const SubscribedFrame& frame);
	Reaction on_unsubscribed(const UnsubscribedFrame& frame);
	Reaction on_ok(const OkFrame& frame);
	Reaction on_error(const ErrorFrame& frame);
	/// Fills in Update::subscription and reports sequence gaps. On an
	/// orderbook_delta gap, `resync` requests fresh snapshots.
	Reaction on_data(WsMessage& message, bool resync);

	[[nodiscard]] std::vector<ws::Subscription> subscriptions() const;

private:
	enum class State : std::uint8_t { Unsent, Pending, Active, Unsubscribing };
	enum class Kind : std::uint8_t { Subscribe, Unsubscribe, Update, List };

	struct Entry {
		ws::Subscription handle;
		ws::SubscribeParams params;
		State state{State::Unsent};
		/// The server's ID once it confirms; 0 before. Kalshi's sids start at 1.
		std::int64_t sid{0};
		std::optional<std::int64_t> last_seq;
		/// update_subscription calls made before the server confirmed.
		std::vector<ws::UpdateSubscriptionParams> held;
		bool unsubscribe_held{false};
		/// Subscribed with a market filter. Once every market is removed, it is
		/// not resubscribed, which would stream every market instead.
		bool filtered{false};
	};

	struct InFlight {
		Kind kind{Kind::Subscribe};
		std::int64_t subscription{0};
		/// For Kind::Update: the change to apply when the server confirms it.
		std::optional<ws::UpdateSubscriptionParams> update;
	};

	std::int64_t next_id() { return next_id_++; }
	Entry* find(std::int64_t subscription);
	void erase(std::int64_t subscription);
	std::optional<InFlight> take(std::optional<std::int64_t> command);
	/// Frames such as `ok` also take a sequence number on sequenced channels.
	void note_seq(std::optional<std::int64_t> sid, std::optional<std::int64_t> seq);
	std::string subscribe_frame(Entry& entry);
	std::string update_frame(Entry& entry, const ws::UpdateSubscriptionParams& params);
	std::string unsubscribe_frame(Entry& entry);

	std::int64_t next_id_{1};
	std::map<std::int64_t, Entry> entries_; // by Subscription::id
	std::unordered_map<std::int64_t, std::int64_t> by_sid_;
	std::map<std::int64_t, InFlight> commands_; // in flight, in the order sent
};

} // namespace kalshi::detail
