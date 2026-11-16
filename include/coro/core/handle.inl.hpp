#pragma once

#include "handle.hpp"
#include "promise_base.hpp"

namespace coro {

template <typename Promise>
CoroHandle CoroHandle::fromTypedHandle(std::coroutine_handle<Promise> handle) {
    Promise& promise = handle.promise();
    return CoroHandle {handle, &promise};
}

inline CoroHandle::CoroHandle(handle_t handle, PromiseBase* promise)
    : _handle(handle)
    , _promise(promise) {
    ++(_promise->_useCount);
}

inline CoroHandle::CoroHandle(std::nullptr_t)
    : _handle(nullptr)
    , _promise(nullptr) {}

inline CoroHandle::~CoroHandle() {
    reset();
}

inline CoroHandle::CoroHandle(const CoroHandle& other) {
    _handle = other._handle;
    _promise = other._promise;
    if (_promise) {
        ++_promise->_useCount;
    }
}

inline CoroHandle::CoroHandle(CoroHandle&& other) noexcept {
    _handle = std::exchange(other._handle, nullptr);
    _promise = std::exchange(other._promise, nullptr);
}

inline CoroHandle& CoroHandle::operator=(const CoroHandle& other) {
    // Take the new reference before dropping the old one: safe for self-assignment and for
    // `other` living inside the frame that the old reference keeps alive.
    CoroHandle copy {other};
    swap(copy);
    return *this;
}

inline CoroHandle& CoroHandle::operator=(CoroHandle&& other) noexcept {
    CoroHandle moved {std::move(other)};
    swap(moved);
    return *this;
}

inline void CoroHandle::swap(CoroHandle& other) noexcept {
    std::swap(_handle, other._handle);
    std::swap(_promise, other._promise);
}

inline void CoroHandle::reset() {
    // Detach first: destroying the frame may destroy the object holding this handle.
    auto handle = std::exchange(_handle, nullptr);
    auto* promise = std::exchange(_promise, nullptr);
    if (handle && --(promise->_useCount) == 0) {
        handle.destroy();
    }
}

template <typename T>
T& CoroHandle::promise() {
    return static_cast<T&>(*_promise);
}

template <typename T>
const T& CoroHandle::promise() const {
    return static_cast<const T&>(*_promise);
}

inline CoroHandle::handle_t CoroHandle::handle() {
    return _handle;
}

inline bool CoroHandle::done() const {
    return _handle.done();
}

inline void CoroHandle::resume() {
    _handle.resume();
}

inline void CoroHandle::throwIfStopped() {
    promise().context.stopToken.throwIfStopped();
}

inline CoroHandle::operator bool() const {
    return static_cast<bool>(_handle);
}

inline bool CoroHandle::operator==(const CoroHandle& other) const noexcept {
    return _handle == other._handle;
}

inline std::strong_ordering CoroHandle::operator<=>(const CoroHandle& other) const noexcept {
    return _handle <=> other._handle;
}

} // namespace coro
