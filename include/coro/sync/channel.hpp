#pragma once

#include "../core/executor.hpp"
#include "../core/promise_base.hpp"
#include "../core/stop.hpp"
#include "../core/traits.hpp"
#include "../detail/containers.hpp"

#include <coroutine>
#include <cstddef>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>

namespace coro {

namespace detail {

template <typename T>
class ChannelState;
template <typename T>
class ChannelSendAwaitable;
template <typename T>
class ChannelReceiveAwaitable;

/// A pending `co_await channel.send(value)`; becomes a ChannelSendAwaitable once co_await(ed).
template <typename T>
struct ChannelSend {
    std::shared_ptr<ChannelState<T>> state;
    T value;
};

/// A pending `co_await channel.receive()`; becomes a ChannelReceiveAwaitable once co_await(ed).
template <typename T>
struct ChannelReceive {
    std::shared_ptr<ChannelState<T>> state;
};

/**
 * State shared by every handle to one channel: a FIFO buffer of at most `capacity` values, and the senders and
 * receivers waiting on it. Everything is guarded by one mutex. A waiter is resumed by scheduling it on its own
 * executor while the mutex is held, as Latch does, so a waiter that is resumed by a stop request and removes itself
 * can never race with the channel completing it.
 */
template <typename T>
class ChannelState {
public:
    using Ref = std::shared_ptr<ChannelState>;

    explicit ChannelState(std::size_t capacity)
        : _capacity(capacity) {}

    enum class SendResult { Sent, Full, Closed };

    /// Hands `value` to a waiting receiver, or buffers it if there is room. Leaves `value` untouched unless Sent.
    SendResult trySend(T& value);

    /// Sends without ever suspending; for the unbounded channel.
    bool send(T value) {
        std::scoped_lock lock {_mutex};
        return trySend(value) == SendResult::Sent;
    }

    void close();

    /// A larger capacity takes the oldest waiting senders' values until the buffer is full again; a smaller one drops
    /// nothing, and the buffer shrinks to it as values are received.
    void setCapacity(std::size_t capacity);

    std::size_t capacity() const {
        std::scoped_lock lock {_mutex};
        return _capacity;
    }

private:
    friend class ChannelSendAwaitable<T>;
    friend class ChannelReceiveAwaitable<T>;

    /// Takes the next value: the oldest buffered one (refilling the buffer from the oldest waiting sender while it is
    /// under the capacity), or the oldest waiting sender's directly when nothing is buffered (rendezvous). False when
    /// there is nothing to take.
    bool tryReceive(std::optional<T>& out);

    mutable std::mutex _mutex;
    std::deque<T> _buffer;
    /// May be below the buffer's size after `setCapacity` lowered it: the buffer then takes no value until receives
    /// bring it under the capacity.
    std::size_t _capacity;
    bool _closed = false;
    Deque<ChannelSendAwaitable<T>*> _senders;
    Deque<ChannelReceiveAwaitable<T>*> _receivers;
};

template <typename T>
class ChannelSendAwaitable {
public:
    ChannelSendAwaitable(ChannelSend<T>&& send, const PromiseBase& promise)
        : _state(std::move(send.state))
        , _value(std::move(send.value))
        , _executor(promise.executor)
        , _stopToken(promise.context.stopToken) {}

    ChannelSendAwaitable(const ChannelSendAwaitable&) = delete;
    ChannelSendAwaitable& operator=(const ChannelSendAwaitable&) = delete;

    bool await_ready() noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        std::scoped_lock lock {_state->_mutex};
        switch (_state->trySend(_value)) {
        case ChannelState<T>::SendResult::Sent:
            _done = true;
            _sent = true;
            return false;
        case ChannelState<T>::SendResult::Closed:
            _done = true;
            return false;
        case ChannelState<T>::SendResult::Full:
            break;
        }
        _continuation = CoroHandle::fromTypedHandle(continuation);
        _state->_senders.pushBack(this);
        // Registered under the channel mutex, so nothing can complete this waiter before the executor knows about it.
        _executor->external(_continuation);
        return true;
    }

    /// True once the value is in the channel, false if the channel was closed first (the value is then dropped).
    /// Throws if the coroutine was stopped while waiting; the value is dropped then too.
    bool await_resume() {
        std::scoped_lock lock {_state->_mutex};
        if (!_done) {
            // resumed by a stop request, not by the channel
            _state->_senders.erase(this);
            _stopToken.throwException();
        }
        return _sent;
    }

private:
    friend class ChannelState<T>;

    /// Called with the channel mutex held, once the value has been taken (true) or the channel closed (false).
    void complete(bool sent) {
        _done = true;
        _sent = sent;
        _executor->schedule(_continuation);
    }

    T& value() {
        return _value;
    }

    typename ChannelState<T>::Ref _state;
    T _value;
    Executor::Ref _executor;
    StopToken _stopToken;
    CoroHandle _continuation;
    bool _done = false;
    bool _sent = false;
};

template <typename T>
class ChannelReceiveAwaitable {
public:
    ChannelReceiveAwaitable(ChannelReceive<T>&& receive, const PromiseBase& promise)
        : _state(std::move(receive.state))
        , _executor(promise.executor)
        , _stopToken(promise.context.stopToken) {}

    ChannelReceiveAwaitable(const ChannelReceiveAwaitable&) = delete;
    ChannelReceiveAwaitable& operator=(const ChannelReceiveAwaitable&) = delete;

    bool await_ready() noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
        std::scoped_lock lock {_state->_mutex};
        if (_state->tryReceive(_result)) {
            _done = true;
            return false;
        }
        if (_state->_closed) {
            _done = true;
            return false;
        }
        _continuation = CoroHandle::fromTypedHandle(continuation);
        _state->_receivers.pushBack(this);
        // Registered under the channel mutex, so nothing can complete this waiter before the executor knows about it.
        _executor->external(_continuation);
        return true;
    }

    /// The next value, or nullopt once the channel is closed and drained.
    /// Throws if the coroutine was stopped while waiting.
    std::optional<T> await_resume() {
        std::scoped_lock lock {_state->_mutex};
        if (!_done) {
            // resumed by a stop request, not by the channel
            _state->_receivers.erase(this);
            _stopToken.throwException();
        }
        return std::move(_result);
    }

private:
    friend class ChannelState<T>;

    /// Called with the channel mutex held: a value, or nullopt when the channel closed.
    void complete(std::optional<T> value) {
        _result = std::move(value);
        _done = true;
        _executor->schedule(_continuation);
    }

    typename ChannelState<T>::Ref _state;
    Executor::Ref _executor;
    StopToken _stopToken;
    CoroHandle _continuation;
    std::optional<T> _result;
    bool _done = false;
};

template <typename T>
typename ChannelState<T>::SendResult ChannelState<T>::trySend(T& value) {
    if (_closed) {
        return SendResult::Closed;
    }
    // A receiver only waits while nothing is buffered, so handing over directly keeps the order.
    if (auto receiver = _receivers.popFront()) {
        (*receiver)->complete(std::move(value));
        return SendResult::Sent;
    }
    if (_buffer.size() < _capacity) {
        _buffer.push_back(std::move(value));
        return SendResult::Sent;
    }
    return SendResult::Full;
}

template <typename T>
bool ChannelState<T>::tryReceive(std::optional<T>& out) {
    if (!_buffer.empty()) {
        out = std::move(_buffer.front());
        _buffer.pop_front();
        // After a lowered capacity the buffer can still be at or over it; its waiting senders keep waiting then.
        if (_buffer.size() < _capacity) {
            if (auto sender = _senders.popFront()) {
                _buffer.push_back(std::move((*sender)->value()));
                (*sender)->complete(true);
            }
        }
        return true;
    }
    if (auto sender = _senders.popFront()) {
        out = std::move((*sender)->value());
        (*sender)->complete(true);
        return true;
    }
    return false;
}

template <typename T>
void ChannelState<T>::setCapacity(std::size_t capacity) {
    std::scoped_lock lock {_mutex};
    _capacity = capacity;
    // Senders wait only while the buffer is full, and receivers only while it is empty with no sender waiting, so
    // room made here goes to the waiting senders, oldest first. A closed channel has no senders left.
    while (_buffer.size() < _capacity) {
        auto sender = _senders.popFront();
        if (!sender) {
            break;
        }
        _buffer.push_back(std::move((*sender)->value()));
        (*sender)->complete(true);
    }
}

template <typename T>
void ChannelState<T>::close() {
    std::scoped_lock lock {_mutex};
    if (_closed) {
        return;
    }
    _closed = true;
    // Receivers wait only while nothing is buffered and no sender waits, so they all see the end of the channel.
    while (auto receiver = _receivers.popFront()) {
        (*receiver)->complete(std::nullopt);
    }
    while (auto sender = _senders.popFront()) {
        (*sender)->complete(false);
    }
}

/// What both channel kinds share: receiving and closing.
template <typename T>
class ChannelBase {
public:
    /// `co_await channel.receive()`: the next value in FIFO order, or std::nullopt once the channel is closed and every
    /// value sent before close() has been received. Suspends while the channel is empty and open.
    [[nodiscard]] ChannelReceive<T> receive() const {
        return ChannelReceive<T> {_state};
    }

    /// Ends the channel: later sends fail, waiting senders resume with false and drop their values, and receivers get
    /// std::nullopt once the values already in the channel are received. Idempotent.
    void close() const {
        _state->close();
    }

protected:
    explicit ChannelBase(std::size_t capacity)
        : _state(std::make_shared<ChannelState<T>>(capacity)) {}

    typename ChannelState<T>::Ref _state;
};

} // namespace detail

/**
 * @brief Bounded multiple-producer multiple-consumer FIFO channel of values of type T, usable across executors and
 * threads. A copy refers to the same channel.
 *
 * `co_await channel.send(value)` suspends while `capacity` values are already waiting to be received, which applies
 * back-pressure to a producer that outruns its consumer. A capacity of 0 makes every send a rendezvous: it completes
 * only once a receiver has taken the value. setCapacity() changes the capacity while the channel is in use.
 *
 * @code
 * coro::BoundedChannel<int> channel {2};
 * // producer
 * for (int i = 0; i < 10; ++i) co_await channel.send(i);
 * channel.close();
 * // consumer
 * while (auto value = co_await channel.receive()) use(*value);
 * @endcode
 */
template <typename T>
class BoundedChannel : public detail::ChannelBase<T> {
public:
    explicit BoundedChannel(std::size_t capacity)
        : detail::ChannelBase<T>(capacity) {}

    /// `co_await channel.send(value)`: true once the value is in the channel, false if the channel is or gets closed
    /// first, in which case the value is dropped.
    [[nodiscard]] detail::ChannelSend<T> send(T value) const {
        return detail::ChannelSend<T> {this->_state, std::move(value)};
    }

    /// Changes how many values the channel holds for its receivers. A larger capacity takes the values of waiting
    /// senders, oldest first, until the channel is full again; those sends complete with true. A smaller capacity drops
    /// nothing: the values already in the channel stay, and sends wait until receives have brought the count below the
    /// new capacity. 0 makes sends rendezvous once the values held are received. Callable from any thread.
    void setCapacity(std::size_t capacity) const {
        this->_state->setCapacity(capacity);
    }

    std::size_t capacity() const {
        return this->_state->capacity();
    }
};

/**
 * @brief Unbounded multiple-producer multiple-consumer FIFO channel of values of type T. send() never suspends and can
 * be called from any thread, coroutine or not. A copy refers to the same channel.
 */
template <typename T>
class Channel : public detail::ChannelBase<T> {
public:
    Channel()
        : detail::ChannelBase<T>(std::numeric_limits<std::size_t>::max()) {}

    /// Puts the value into the channel, or hands it to a waiting receiver. False if the channel is closed, in which
    /// case the value is dropped.
    bool send(T value) const {
        return this->_state->send(std::move(value));
    }
};

template <typename T>
struct await_ready_trait<detail::ChannelSend<T>> {
    static detail::ChannelSendAwaitable<T> await_transform(const PromiseBase& promise, detail::ChannelSend<T>&& send) {
        return detail::ChannelSendAwaitable<T> {std::move(send), promise};
    }
};

template <typename T>
struct await_ready_trait<detail::ChannelReceive<T>> {
    static detail::ChannelReceiveAwaitable<T> await_transform(const PromiseBase& promise,
                                                              detail::ChannelReceive<T>&& receive) {
        return detail::ChannelReceiveAwaitable<T> {std::move(receive), promise};
    }
};

} // namespace coro
