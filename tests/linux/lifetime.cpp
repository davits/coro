#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/sleep.hpp>

#include <gtest/gtest.h>

#include <vector>

class CountingExecutor : public coro::SerialExecutor {
public:
    using Ref = std::shared_ptr<CountingExecutor>;
    static Ref create(std::shared_ptr<std::atomic<size_t>> counter) {
        return std::make_shared<CountingExecutor>(Tag {}, std::move(counter));
    }

protected:
    using Tag = coro::SerialExecutor::Tag;

public:
    CountingExecutor(Tag, std::shared_ptr<std::atomic<size_t>> counter)
        : coro::SerialExecutor(Tag {})
        , _counter(std::move(counter)) {
        ++*_counter;
    }

    ~CountingExecutor() {
        --*_counter;
    }

private:
    std::shared_ptr<std::atomic<size_t>> _counter;
};

template <size_t N>
coro::Task<int> foo(bool timeout) {
    co_return co_await foo<N - 1>(timeout);
}

template <>
coro::Task<int> foo<0>(bool timeout) {
    if (timeout) {
        co_await coro::sleep(500);
    }
    co_return 42;
}

TEST(Simple, Lifetime) {
    auto counter = std::make_shared<std::atomic<size_t>>(0);
    int result = 0;
    {
        auto executor = CountingExecutor::create(counter);
        auto task = foo<5>(false);
        result = executor->syncWait(std::move(task));
        EXPECT_EQ(*counter, 1);
    }
    // syncWait() returns as soon as the task value is set, at which point the executor thread can still
    // hold the last reference to the wrapper task and hence to the executor itself, so give it time.
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);
    // Make sure that executor was destroyed despite the fact that there are circular shared references
    // between executor and task promises.
    EXPECT_EQ(*counter, 0);
    EXPECT_EQ(result, 42);
}

TEST(NoReference, Lifetime) {
    auto counter = std::make_shared<std::atomic<size_t>>(0);
    int result = 0;

    auto executor = CountingExecutor::create(counter);
    auto task = foo<5>(true);
    auto future = executor->future(std::move(task));
    EXPECT_EQ(*counter, 1);
    executor.reset();
    EXPECT_EQ(*counter, 1); // still there because there is a scheduled task
    using namespace std::chrono_literals;
    EXPECT_EQ(future.wait_for(200ms), std::future_status::timeout);
    // future is still not satisfied after 200ms and the executor is still alive,
    // because there is a task <-> executor cyclic dependency keeping it alive
    EXPECT_EQ(*counter, 1);
    result = future.get(); // wait for future to fulfill
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(*counter, 0); // executor was destroyed
    EXPECT_EQ(result, 42);
}

namespace {

// Counts live coroutine frames: a by-value coroutine parameter is copied into the frame and dies with it.
struct FrameTracker {
    explicit FrameTracker(int& alive)
        : _alive(&alive) {
        ++*_alive;
    }

    FrameTracker(const FrameTracker& other)
        : _alive(other._alive) {
        ++*_alive;
    }

    ~FrameTracker() {
        --*_alive;
    }

private:
    int* _alive;
};

coro::Task<int> tracked(FrameTracker) {
    co_return 1;
}

// Records the address of the frame's copy of the parameter, i.e. of a Task stored inside a coroutine frame.
struct TaskHolder {
    static inline TaskHolder* inFrame = nullptr;

    explicit TaskHolder(coro::Task<int> task)
        : task(std::move(task)) {}

    TaskHolder(TaskHolder&& other)
        : task(std::move(other.task)) {
        inFrame = this;
    }

    coro::Task<int> task;
};

coro::Task<int> holding(FrameTracker, TaskHolder) {
    co_return 2;
}

} // namespace

TEST(MoveAssign, Lifetime) {
    int alive = 0;
    auto task = tracked(FrameTracker {alive});
    EXPECT_EQ(alive, 1);
    // must release the frame the task was holding
    task = tracked(FrameTracker {alive});
    EXPECT_EQ(alive, 1);
    task.reset();
    EXPECT_EQ(alive, 0);
}

TEST(ContainerErase, Lifetime) {
    int alive = 0;
    std::vector<coro::Task<int>> tasks;
    for (int i = 0; i < 4; ++i) {
        tasks.push_back(tracked(FrameTracker {alive}));
    }
    EXPECT_EQ(alive, 4);
    // erase shifts the remaining elements down by move assignment
    tasks.erase(tasks.begin());
    EXPECT_EQ(alive, 3);
    tasks.clear();
    EXPECT_EQ(alive, 0);
}

TEST(SelfMoveAssign, Lifetime) {
    int alive = 0;
    auto task = tracked(FrameTracker {alive});
    auto& self = task;
    task = std::move(self);
    EXPECT_TRUE(task);
    EXPECT_EQ(alive, 1);
}

TEST(SelfCopyAssign, Lifetime) {
    int alive = 0;
    auto task = tracked(FrameTracker {alive});
    auto& self = task;
    task = self;
    EXPECT_TRUE(task);
    EXPECT_EQ(alive, 1);
}

TEST(AssignFromOwnedFrame, Lifetime) {
    int outer = 0;
    int inner = 0;
    auto task = holding(FrameTracker {outer}, TaskHolder {tracked(FrameTracker {inner})});
    EXPECT_EQ(outer, 1);
    EXPECT_EQ(inner, 1);
    // The source lives inside the frame which the assignment releases,
    // so it has to be taken before the frame is destroyed.
    task = std::move(TaskHolder::inFrame->task);
    EXPECT_TRUE(task);
    EXPECT_EQ(outer, 0);
    EXPECT_EQ(inner, 1);
    task.reset();
    EXPECT_EQ(inner, 0);
}
