#pragma once

#include <coroutine>

namespace coro {

class PromiseBase;

class CoroHandle {
public:
    using handle_t = std::coroutine_handle<>;

public:
    template <typename Promise>
    static CoroHandle fromTypedHandle(std::coroutine_handle<Promise> handle);

private:
    CoroHandle(handle_t handle, PromiseBase* promise);

public:
    CoroHandle() = default;

    CoroHandle(nullptr_t);

    ~CoroHandle();

    CoroHandle(const CoroHandle& other);

    CoroHandle(CoroHandle&& other);

    CoroHandle& operator=(const CoroHandle& other);

    CoroHandle& operator=(CoroHandle&& other);

public:
    template <typename T = PromiseBase>
    T& promise();

    template <typename T = PromiseBase>
    const T& promise() const;

    handle_t handle();

    bool done() const;

    void resume();

    void reset();

public:
    explicit operator bool();

    bool operator==(const CoroHandle& other) const noexcept;

    std::strong_ordering operator<=>(const CoroHandle& other) const noexcept;

private:
    handle_t _handle = nullptr;
    PromiseBase* _promise = nullptr;
};

} // namespace coro
