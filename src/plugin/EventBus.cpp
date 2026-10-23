#include "kh/plugin/EventBus.h"

#include <deque>
#include <exception>
#include <map>
#include <string>
#include <vector>

namespace kh::plugin {

// The bus keeps its data in a shared State, so Subscriptions can hold a weak
// pointer and stay safe to destroy after the bus is gone.
struct Subscription::State {
    struct Listener {
        std::string type;
        std::shared_ptr<const std::function<void(const void*)>> fn;  // shared: may be erased while running
    };
    struct Pending {
        std::string type;
        std::shared_ptr<const void> event;
    };

    std::map<std::uint64_t, Listener> listeners;  // by id, so in subscription order
    std::uint64_t nextId = 1;
    std::deque<Pending> queue;
    bool delivering = false;
    std::function<void(const Diagnostic&)> onError;

    void deliver(const Pending& p) {
        // Snapshot who listens now; listeners added during delivery start with the next event.
        std::vector<std::uint64_t> ids;
        for (const auto& [id, l] : listeners) {
            if (l.type == p.type) ids.push_back(id);
        }
        for (auto id : ids) {
            auto it = listeners.find(id);
            if (it == listeners.end()) continue;  // disconnected by an earlier listener
            auto fn = it->second.fn;              // keep alive even if it disconnects itself
            try {
                (*fn)(p.event.get());
            } catch (const std::exception& e) {
                report(p.type, e.what());
            } catch (...) {
                report(p.type, "unknown error");
            }
        }
    }

    void report(const std::string& type, const std::string& what) {
        if (!onError) return;
        onError({"event.listener_failed", Severity::Error,
                 "Part of Khan had a problem while reacting to a change. What you see may be out of date; "
                 "save your work and restart Khan if it looks wrong.",
                 type + ": " + what});
    }
};

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------

Subscription::Subscription(Subscription&& other) noexcept
    : bus_(std::move(other.bus_)), id_(std::exchange(other.id_, 0)) {}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        bus_ = std::move(other.bus_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void Subscription::reset() {
    if (auto state = bus_.lock()) state->listeners.erase(id_);
    bus_.reset();
    id_ = 0;
}

bool Subscription::connected() const {
    auto state = bus_.lock();
    return state && state->listeners.count(id_) != 0;
}

// ---------------------------------------------------------------------------
// EventBus
// ---------------------------------------------------------------------------

EventBus::EventBus() : state_(std::make_shared<Subscription::State>()) {}

EventBus::~EventBus() = default;

void EventBus::setErrorHandler(std::function<void(const Diagnostic&)> handler) {
    state_->onError = std::move(handler);
}

std::size_t EventBus::listenerCount(std::string_view type) const {
    std::size_t n = 0;
    for (const auto& [id, l] : state_->listeners) {
        if (l.type == type) ++n;
    }
    return n;
}

Subscription EventBus::subscribeRaw(std::string_view type, std::function<void(const void*)> listener) {
    const auto id = state_->nextId++;
    state_->listeners.emplace(
        id, Subscription::State::Listener{
                std::string(type),
                std::make_shared<const std::function<void(const void*)>>(std::move(listener))});
    return Subscription(state_, id);
}

void EventBus::publishRaw(std::string_view type, std::shared_ptr<const void> event) {
    // Keep the state alive even if a listener destroys the bus.
    auto state = state_;
    state->queue.push_back({std::string(type), std::move(event)});
    if (state->delivering) return;  // the outer publish delivers it, in order

    state->delivering = true;
    while (!state->queue.empty()) {
        auto next = std::move(state->queue.front());
        state->queue.pop_front();
        state->deliver(next);
    }
    state->delivering = false;
}

}  // namespace kh::plugin
