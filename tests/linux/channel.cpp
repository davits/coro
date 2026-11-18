#include <coro/coro.hpp>
#include <coro/sync/channel.hpp>
#include <coro/executors/serial_executor.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

coro::Task<void> sendAll(coro::BoundedChannel<int> channel, int count, std::atomic<int>& sent) {
    for (int i = 0; i < count; ++i) {
        const bool ok = co_await channel.send(i);
        EXPECT_TRUE(ok);
        ++sent;
    }
    channel.close();
}

template <typename Channel>
coro::Task<std::vector<int>> receiveAll(Channel channel) {
    std::vector<int> values;
    while (auto value = co_await channel.receive()) {
        values.push_back(*value);
    }
    co_return values;
}

coro::Task<std::optional<int>> receiveOne(coro::BoundedChannel<int> channel) {
    co_return co_await channel.receive();
}

coro::Task<bool> sendOne(coro::BoundedChannel<int> channel, int value) {
    co_return co_await channel.send(value);
}

std::vector<int> iota(int count) {
    std::vector<int> values;
    for (int i = 0; i < count; ++i)
        values.push_back(i);
    return values;
}

} // namespace

TEST(BoundedChannel, KeepsFifoOrder) {
    coro::BoundedChannel<int> channel {2};
    auto executor = coro::SerialExecutor::create();
    std::atomic<int> sent = 0;
    executor->schedule(sendAll(channel, 10, sent));
    EXPECT_EQ(executor->syncWait(receiveAll(channel)), iota(10));
    EXPECT_EQ(sent, 10);
}

TEST(BoundedChannel, SendWaitsWhileFull) {
    coro::BoundedChannel<int> channel {2};
    auto producer = coro::SerialExecutor::create();
    std::atomic<int> sent = 0;
    producer->schedule(sendAll(channel, 5, sent));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(sent, 2);

    auto consumer = coro::SerialExecutor::create();
    EXPECT_EQ(consumer->syncWait(receiveAll(channel)), iota(5));
    producer->drain();
    EXPECT_EQ(sent, 5);
}

TEST(BoundedChannel, ZeroCapacityIsARendezvous) {
    coro::BoundedChannel<int> channel {0};
    auto producer = coro::SerialExecutor::create();
    auto sending = producer->future(sendOne(channel, 7));
    EXPECT_EQ(sending.wait_for(50ms), std::future_status::timeout);

    auto consumer = coro::SerialExecutor::create();
    EXPECT_EQ(consumer->syncWait(receiveOne(channel)), 7);
    EXPECT_TRUE(sending.get());
}

TEST(BoundedChannel, CloseEndsWaitingReceivers) {
    coro::BoundedChannel<int> channel {1};
    auto executor = coro::SerialExecutor::create();
    auto receiving = executor->future(receiveOne(channel));
    EXPECT_EQ(receiving.wait_for(50ms), std::future_status::timeout);
    channel.close();
    EXPECT_EQ(receiving.get(), std::nullopt);
}

TEST(BoundedChannel, CloseFailsWaitingSenders) {
    coro::BoundedChannel<int> channel {0};
    auto executor = coro::SerialExecutor::create();
    auto sending = executor->future(sendOne(channel, 1));
    EXPECT_EQ(sending.wait_for(50ms), std::future_status::timeout);
    channel.close();
    EXPECT_FALSE(sending.get());
    EXPECT_FALSE(executor->syncWait(sendOne(channel, 2)));
}

TEST(BoundedChannel, ReceiversDrainValuesSentBeforeClose) {
    coro::BoundedChannel<int> channel {3};
    auto executor = coro::SerialExecutor::create();
    EXPECT_TRUE(executor->syncWait(sendOne(channel, 1)));
    EXPECT_TRUE(executor->syncWait(sendOne(channel, 2)));
    channel.close();
    EXPECT_EQ(executor->syncWait(receiveAll(channel)), (std::vector<int> {1, 2}));
}

TEST(BoundedChannel, ProducerAndConsumerOnTwoExecutors) {
    coro::BoundedChannel<int> channel {4};
    auto producer = coro::SerialExecutor::create();
    auto consumer = coro::SerialExecutor::create();
    std::atomic<int> sent = 0;
    producer->schedule(sendAll(channel, 1000, sent));
    EXPECT_EQ(consumer->syncWait(receiveAll(channel)), iota(1000));
    producer->drain();
    EXPECT_EQ(sent, 1000);
}

TEST(BoundedChannel, StopWhileReceiving) {
    coro::BoundedChannel<int> channel {1};
    auto executor = coro::SerialExecutor::create();
    auto stop = coro::StopSource {};
    auto receiving = executor->future(receiveOne(channel).setStopToken(stop.token()));
    EXPECT_EQ(receiving.wait_for(50ms), std::future_status::timeout);
    stop.requestStop();
    EXPECT_THROW((void)receiving.get(), coro::StopError);

    // The stopped receiver left the channel: the next value goes to the next receiver.
    EXPECT_TRUE(executor->syncWait(sendOne(channel, 3)));
    EXPECT_EQ(executor->syncWait(receiveOne(channel)), 3);
}

TEST(BoundedChannel, StopWhileSending) {
    coro::BoundedChannel<int> channel {0};
    auto executor = coro::SerialExecutor::create();
    auto stop = coro::StopSource {};
    auto sending = executor->future(sendOne(channel, 1).setStopToken(stop.token()));
    EXPECT_EQ(sending.wait_for(50ms), std::future_status::timeout);
    stop.requestStop();
    EXPECT_THROW((void)sending.get(), coro::StopError);

    // The stopped sender's value is gone.
    channel.close();
    EXPECT_EQ(executor->syncWait(receiveOne(channel)), std::nullopt);
}

TEST(Channel, ThreadSendsCoroutineReceives) {
    coro::Channel<int> channel;
    auto executor = coro::SerialExecutor::create();
    auto receiving = executor->future(receiveAll(channel));
    std::thread producer([channel] {
        for (int i = 0; i < 100; ++i) {
            EXPECT_TRUE(channel.send(i));
        }
        channel.close();
    });
    producer.join();
    EXPECT_EQ(receiving.get(), iota(100));
    EXPECT_FALSE(channel.send(100));
}
