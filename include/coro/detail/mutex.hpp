#pragma once

#include "../promise.hpp"

namespace coro {

class Mutex {
public:
    void scoped_lock();

    void unlock();
};

class MutexAwaitable {
public:
    MutexAwaitable(Mutex& mutex)
        : _mutex(mutex) {}

private:
    Mutex& _mutex;
};

template <>
struct await_ready_trait<Mutex> {
    static MutexAwaitable await_transform(Executor*, Mutex& awaitable) {
        return MutexAwaitable {awaitable};
    }
};

} // namespace coro
