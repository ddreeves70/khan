#pragma once

/// @file
/// The event bus: one place where parts of Khan announce what happened.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "kh/core/Diagnostic.h"

namespace kh::plugin {

class EventBus;

/// Keeps a listener connected. Destroying it (or calling reset()) disconnects.
///
/// It is safe to destroy a Subscription after its EventBus is gone.
class Subscription {
public:
    /// Makes an empty subscription that is not connected to anything.
    Subscription() = default;
    /// Disconnects the listener.
    ~Subscription() { reset(); }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /// Takes over another subscription.
    /// @param other The subscription to take; it becomes empty.
    Subscription(Subscription&& other) noexcept;

    /// Disconnects this listener and takes over another subscription.
    /// @param other The subscription to take; it becomes empty.
    /// @return This subscription.
    Subscription& operator=(Subscription&& other) noexcept;

    /// Disconnects the listener now. It will not be called again, even for an
    /// event that is being delivered right now.
    void reset();

    /// Checks whether a listener is connected.
    /// @return True until reset() or the bus is destroyed.
    bool connected() const;

private:
    friend class EventBus;
    struct State;
    Subscription(std::weak_ptr<State> bus, std::uint64_t id) : bus_(std::move(bus)), id_(id) {}
    std::weak_ptr<State> bus_;
    std::uint64_t id_ = 0;
};

/// Sends events from the part of Khan where something happened to everyone
/// listening, so one input can update every view.
///
/// Events are delivered in the order they were published. An event published
/// by a listener waits until the current event has reached every listener. The
/// bus is not thread-safe: use it from the UI thread (or the one thread of
/// kh_cli). A listener that throws is reported to the error handler, and the
/// other listeners still get the event.
///
/// @code
/// EventBus bus;
/// Subscription s = bus.subscribe<DocumentChanged>([](const DocumentChanged& e) {
///     // update a panel for e.doc
/// });
/// bus.publish(DocumentChanged{doc.id(), ChangeKind::Executed, "Add Box1"});
/// @endcode
class EventBus {
public:
    /// Makes a bus with no listeners.
    EventBus();
    /// Disconnects every listener.
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// Starts listening for one type of event.
    /// @tparam E The event type, a struct from Events.h with a `kType` name.
    /// @param listener Called with each event of type @p E.
    /// @return Keep this; the listener stays connected while it exists.
    template <typename E>
    [[nodiscard]] Subscription subscribe(std::function<void(const E&)> listener) {
        return subscribeRaw(E::kType,
                            [fn = std::move(listener)](const void* e) { fn(*static_cast<const E*>(e)); });
    }

    /// Sends an event to every listener for its type.
    /// @tparam E The event type.
    /// @param event The event. It is copied, so it may be a temporary.
    template <typename E>
    void publish(const E& event) {
        publishRaw(E::kType, std::make_shared<const E>(event));
    }

    /// Sets what happens when a listener throws. The default ignores it.
    /// @param handler Called with a Diagnostic that names the event type.
    void setErrorHandler(std::function<void(const Diagnostic&)> handler);

    /// Counts the listeners for one event type, for tests and the Plugin manager.
    /// @param type An event type name, e.g. DocumentChanged::kType.
    /// @return The number of connected listeners.
    std::size_t listenerCount(std::string_view type) const;

private:
    Subscription subscribeRaw(std::string_view type, std::function<void(const void*)> listener);
    void publishRaw(std::string_view type, std::shared_ptr<const void> event);

    std::shared_ptr<Subscription::State> state_;
};

}  // namespace kh::plugin
