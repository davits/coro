#pragma once

#include <coroutine>
#include <cstddef>

namespace coro {

class PromiseBase;

/**
 * Wrapper class for coroutine handle providing shared lifetime.
 * Coroutine handle will live as long as there is a at least single CoroHandle object to it.
 * Uses reference counting to keep track of the referring objects. Stores reference counter
 * in the promise, thus every CoroHandle object created from coroutine handle via fromTypedHandle() will share
 * the same reference counting.
 * Upon creation extracts and stores pointer to the promise object via PromiseBase class, so unlike the
 * typeless coroutine handle it is possible to get PromiseBase* from it, or the full promise type if it is known.
 */
class CoroHandle {
public:
    using handle_t = std::coroutine_handle<>;

public:
    /**
     * Create CoroHandle wrapper for fully typed coroutine handle.
     * This function is not thread safe, in a sense that one shouldn't create CoroHandle for a coroutine which is being
     * actively destroyed on another thread. So this function should be called only from await_suspend() or from
     * similar places to ensure that coroutine lives while this function is being executed.
     */
    template <typename Promise>
    static CoroHandle fromTypedHandle(std::coroutine_handle<Promise> handle);

    /// Resets state of the object as if it was destroyed, decrements coroutine handle reference counter
    /// and destroys coroutine frame if it has reached 0.
    void reset();

public:
    /// Return the stored promise static casted to the required type, which is PromiseBase by default.
    /// If the type needs to be checked user should get PromiseBase* and dynamic cast to ensure correctness.
    template <typename T = PromiseBase>
    T& promise();

    template <typename T = PromiseBase>
    const T& promise() const;

    /// Returns stored typeless coroutine handle
    handle_t handle();

    /// Returns whether stored coroutine has reached its end point
    bool done() const;

    /// Resumes stored coroutine.
    void resume();

public:
    /// Returns whether stored handle is non null.
    explicit operator bool() const;

    bool operator==(const CoroHandle& other) const noexcept;

    std::strong_ordering operator<=>(const CoroHandle& other) const noexcept;

public:
    CoroHandle() = default;

    CoroHandle(std::nullptr_t);

    ~CoroHandle();

    CoroHandle(const CoroHandle& other);

    CoroHandle(CoroHandle&& other);

    CoroHandle& operator=(const CoroHandle& other);

    CoroHandle& operator=(CoroHandle&& other);

private:
    CoroHandle(handle_t handle, PromiseBase* promise);

private:
    handle_t _handle = nullptr;
    PromiseBase* _promise = nullptr;
};

} // namespace coro
