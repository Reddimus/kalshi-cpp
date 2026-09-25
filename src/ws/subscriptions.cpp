#include "subscriptions.hpp"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "wire.hpp"

namespace kalshi::detail {

namespace {

template <typename T>
struct IsUpdate : std::false_type {};

template <typename Msg>
struct IsUpdate<ws::Update<Msg>> : std::true_type {};

// Adds values missing from `list`, or removes `values` from it.
void merge(std::vector<std::string>& list, const std::vector<std::string>& values,
		   const std::optional<std::string>& single, bool add) {
	std::vector<std::string> changes = values;
	if (single) {
		changes.push_back(*single);
	}
	for (const std::string& value : changes) {
		const std::vector<std::string>::iterator found = std::find(list.begin(), list.end(), value);
		if (add && found == list.end()) {
			list.push_back(value);
		} else if (!add && found != list.end()) {
			list.erase(found);
		}
	}
}

// Keeps the parameters a resubscribe sends in step with successful updates.
void apply(ws::SubscribeParams& params, const ws::UpdateSubscriptionParams& update) {
	switch (update.action) {
		case ws::UpdateAction::AddMarkets:
		case ws::UpdateAction::DeleteMarkets: {
			if (params.market_ticker) {
				params.market_tickers.insert(params.market_tickers.begin(), *params.market_ticker);
				params.market_ticker.reset();
			}
			if (params.market_id) {
				params.market_ids.insert(params.market_ids.begin(), *params.market_id);
				params.market_id.reset();
			}
			const bool add = update.action == ws::UpdateAction::AddMarkets;
			merge(params.market_tickers, update.market_tickers, update.market_ticker, add);
			merge(params.market_ids, update.market_ids, update.market_id, add);
			break;
		}
		case ws::UpdateAction::SubscribeIndices:
		case ws::UpdateAction::UnsubscribeIndices:
			merge(params.index_ids, update.index_ids, std::nullopt,
				  update.action == ws::UpdateAction::SubscribeIndices);
			break;
		case ws::UpdateAction::SubscribeUnderlyings:
		case ws::UpdateAction::UnsubscribeUnderlyings:
			merge(params.underlying_tickers, update.underlying_tickers, std::nullopt,
				  update.action == ws::UpdateAction::SubscribeUnderlyings);
			break;
		default:
			break; // snapshots and lists leave the subscription as it is
	}
}

Error unknown_subscription(ws::Subscription subscription) {
	return Error{ErrorCode::InvalidRequest,
				 "Unknown subscription " + std::to_string(subscription.id),
				 0,
				 {}};
}

} // namespace

ws::Subscription SubscriptionRegistry::subscribe(ws::Channel channel, ws::SubscribeParams params,
												 bool connected, std::vector<std::string>& frames) {
	const ws::Subscription handle{next_id(), channel};
	Entry& entry = entries_[handle.id];
	entry.handle = handle;
	entry.params = std::move(params);
	if (connected) {
		frames.push_back(subscribe_frame(entry));
	}
	return handle;
}

Result<void> SubscriptionRegistry::unsubscribe(ws::Subscription subscription,
											   std::vector<std::string>& frames) {
	Entry* entry = find(subscription.id);
	if (entry == nullptr) {
		return std::unexpected(unknown_subscription(subscription));
	}
	switch (entry->state) {
		case State::Unsent:
			erase(subscription.id);
			break;
		case State::Pending:
			entry->unsubscribe_held = true;
			entry->held.clear();
			break;
		case State::Active:
			frames.push_back(unsubscribe_frame(*entry));
			break;
		case State::Unsubscribing:
			break;
	}
	return {};
}

Result<void> SubscriptionRegistry::update(ws::Subscription subscription,
										  const ws::UpdateSubscriptionParams& params,
										  std::vector<std::string>& frames) {
	if (params.action == ws::UpdateAction::Unknown) {
		return std::unexpected(
			Error{ErrorCode::InvalidRequest, "UpdateSubscriptionParams.action is required", 0, {}});
	}
	Entry* entry = find(subscription.id);
	if (entry == nullptr) {
		return std::unexpected(unknown_subscription(subscription));
	}
	apply(entry->params, params);
	switch (entry->state) {
		case State::Unsent: // the next subscribe carries the change
		case State::Unsubscribing:
			break;
		case State::Pending:
			entry->held.push_back(params);
			break;
		case State::Active:
			frames.push_back(update_frame(*entry, params));
			break;
	}
	return {};
}

std::int64_t SubscriptionRegistry::list_subscriptions(std::vector<std::string>& frames) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::List, 0};
	frames.push_back(render_command(BareCommand{id, "list_subscriptions"}));
	return id;
}

void SubscriptionRegistry::on_connected(std::vector<std::string>& frames) {
	on_disconnected();
	for (std::pair<const std::int64_t, Entry>& item : entries_) {
		frames.push_back(subscribe_frame(item.second));
	}
}

void SubscriptionRegistry::on_disconnected() {
	commands_.clear();
	by_sid_.clear();
	std::erase_if(entries_, [](const std::pair<const std::int64_t, Entry>& item) {
		return item.second.state == State::Unsubscribing || item.second.unsubscribe_held;
	});
	for (std::pair<const std::int64_t, Entry>& item : entries_) {
		Entry& entry = item.second;
		entry.state = State::Unsent;
		entry.sid = 0;
		entry.last_seq.reset();
		entry.held.clear(); // `params` already includes them
	}
}

void SubscriptionRegistry::clear() {
	entries_.clear();
	by_sid_.clear();
	commands_.clear();
}

Reaction SubscriptionRegistry::on_subscribed(const SubscribedFrame& frame) {
	Reaction reaction;
	const std::optional<InFlight> command = take(frame.id);
	Entry* entry =
		command && command->kind == Kind::Subscribe ? find(command->subscription) : nullptr;
	if (entry == nullptr) {
		// Nothing here wants this subscription any more; end it.
		const std::int64_t id = next_id();
		commands_[id] = InFlight{Kind::Unsubscribe, 0};
		reaction.frames.push_back(
			render_command(Command<SidsWire>{id, "unsubscribe", {{frame.sid}}}));
		return reaction;
	}
	entry->sid = frame.sid;
	entry->state = State::Active;
	entry->last_seq.reset();
	by_sid_[frame.sid] = entry->handle.id;
	if (entry->unsubscribe_held) {
		entry->unsubscribe_held = false;
		reaction.frames.push_back(unsubscribe_frame(*entry));
		return reaction;
	}
	for (const ws::UpdateSubscriptionParams& params : entry->held) {
		reaction.frames.push_back(update_frame(*entry, params));
	}
	entry->held.clear();
	reaction.message = ws::Subscribed{entry->handle, frame.sid};
	return reaction;
}

Reaction SubscriptionRegistry::on_unsubscribed(const UnsubscribedFrame& frame) {
	Reaction reaction;
	const std::optional<InFlight> command = take(frame.id);
	std::int64_t subscription = command ? command->subscription : 0;
	if (subscription == 0 && frame.sid) {
		const std::unordered_map<std::int64_t, std::int64_t>::const_iterator found =
			by_sid_.find(*frame.sid);
		subscription = found == by_sid_.end() ? 0 : found->second;
	}
	if (const Entry* entry = find(subscription)) {
		reaction.message = ws::Unsubscribed{entry->handle};
		erase(subscription);
	}
	return reaction;
}

Reaction SubscriptionRegistry::on_ok(const OkFrame& frame) {
	Reaction reaction;
	const std::optional<InFlight> command = take(frame.id);
	if (!command) {
		return reaction;
	}
	if (command->kind == Kind::List) {
		// take() found the command, so the reply carried its id.
		reaction.message =
			ws::SubscriptionList{frame.id.value_or(0), parse_subscription_list(frame.msg)};
		return reaction;
	}
	Entry* entry = command->kind == Kind::Update ? find(command->subscription) : nullptr;
	if (entry == nullptr) {
		return reaction;
	}
	ws::Updated updated = parse_updated(frame.msg);
	updated.subscription = entry->handle;
	// The reply lists every market afterwards; trust it over local bookkeeping.
	if (!updated.market_tickers.empty()) {
		entry->params.market_ticker.reset();
		entry->params.market_tickers = updated.market_tickers;
	}
	if (!updated.market_ids.empty()) {
		entry->params.market_id.reset();
		entry->params.market_ids = updated.market_ids;
	}
	reaction.message = std::move(updated);
	return reaction;
}

Reaction SubscriptionRegistry::on_error(const ErrorFrame& frame) {
	Reaction reaction;
	WsError error{frame.code, frame.message, frame.id, frame.sid, frame.seq, std::nullopt};
	if (error.message.empty()) {
		error.message = std::string{ws::error_code_name(frame.code)};
	}
	const std::optional<InFlight> command = take(frame.id);
	std::int64_t subscription = command ? command->subscription : 0;
	if (subscription == 0 && frame.sid) {
		const std::unordered_map<std::int64_t, std::int64_t>::const_iterator found =
			by_sid_.find(*frame.sid);
		subscription = found == by_sid_.end() ? 0 : found->second;
	}
	if (const Entry* entry = find(subscription)) {
		error.subscription = entry->handle;
		if (command && (command->kind == Kind::Subscribe || command->kind == Kind::Unsubscribe)) {
			erase(subscription); // the server holds no such subscription
		}
	}
	reaction.error = std::move(error);
	return reaction;
}

Reaction SubscriptionRegistry::on_data(WsMessage& message, bool resync) {
	Reaction reaction;
	std::visit(
		[&]<typename T>(T& update) {
			if constexpr (IsUpdate<T>::value) {
				const std::unordered_map<std::int64_t, std::int64_t>::const_iterator found =
					by_sid_.find(update.sid);
				Entry* entry = found == by_sid_.end() ? nullptr : find(found->second);
				if (entry == nullptr) {
					return;
				}
				update.subscription = entry->handle.id;
				if (!update.seq) {
					return;
				}
				constexpr bool snapshot = std::is_same_v<T, ws::Update<ws::OrderbookSnapshot>>;
				if (!snapshot && entry->last_seq && *update.seq != *entry->last_seq + 1) {
					reaction.error = WsError{
						0,
						"Missed messages on subscription " + std::to_string(entry->handle.id) +
							": expected seq " + std::to_string(*entry->last_seq + 1) + ", got " +
							std::to_string(*update.seq),
						std::nullopt,
						update.sid,
						update.seq,
						entry->handle};
					if (resync && entry->handle.channel == ws::Channel::OrderbookDelta &&
						entry->state == State::Active) {
						ws::UpdateSubscriptionParams snapshots;
						snapshots.action = ws::UpdateAction::GetSnapshot;
						snapshots.market_tickers = entry->params.market_tickers;
						if (entry->params.market_ticker) {
							snapshots.market_tickers.push_back(*entry->params.market_ticker);
						}
						if (!snapshots.market_tickers.empty()) {
							reaction.frames.push_back(update_frame(*entry, snapshots));
						}
					}
				}
				entry->last_seq = update.seq;
			}
		},
		message);
	return reaction;
}

std::vector<ws::Subscription> SubscriptionRegistry::subscriptions() const {
	std::vector<ws::Subscription> handles;
	handles.reserve(entries_.size());
	for (const std::pair<const std::int64_t, Entry>& item : entries_) {
		if (item.second.state != State::Unsubscribing && !item.second.unsubscribe_held) {
			handles.push_back(item.second.handle);
		}
	}
	return handles;
}

SubscriptionRegistry::Entry* SubscriptionRegistry::find(std::int64_t subscription) {
	const std::map<std::int64_t, Entry>::iterator found = entries_.find(subscription);
	return found == entries_.end() ? nullptr : &found->second;
}

void SubscriptionRegistry::erase(std::int64_t subscription) {
	const std::map<std::int64_t, Entry>::iterator found = entries_.find(subscription);
	if (found == entries_.end()) {
		return;
	}
	if (found->second.sid != 0) {
		by_sid_.erase(found->second.sid);
	}
	entries_.erase(found);
}

std::optional<SubscriptionRegistry::InFlight>
SubscriptionRegistry::take(std::optional<std::int64_t> command) {
	if (!command) {
		return std::nullopt;
	}
	const std::unordered_map<std::int64_t, InFlight>::iterator found = commands_.find(*command);
	if (found == commands_.end()) {
		return std::nullopt;
	}
	const InFlight in_flight = found->second;
	commands_.erase(found);
	return in_flight;
}

std::string SubscriptionRegistry::subscribe_frame(Entry& entry) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::Subscribe, entry.handle.id};
	entry.state = State::Pending;
	entry.sid = 0;
	entry.last_seq.reset();
	return render_command(Command<SubscribeWire>{
		id, "subscribe", to_wire(std::vector<ws::Channel>{entry.handle.channel}, entry.params)});
}

std::string SubscriptionRegistry::update_frame(Entry& entry,
											   const ws::UpdateSubscriptionParams& params) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::Update, entry.handle.id};
	return render_command(Command<UpdateSubscriptionWire>{
		id, "update_subscription", to_wire(std::vector<std::int64_t>{entry.sid}, params)});
}

std::string SubscriptionRegistry::unsubscribe_frame(Entry& entry) {
	const std::int64_t id = next_id();
	commands_[id] = InFlight{Kind::Unsubscribe, entry.handle.id};
	entry.state = State::Unsubscribing;
	return render_command(Command<SidsWire>{id, "unsubscribe", SidsWire{{entry.sid}}});
}

} // namespace kalshi::detail
