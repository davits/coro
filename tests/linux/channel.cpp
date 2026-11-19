#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/all.hpp>
#include <coro/sleep.hpp>
#include <coro/sync/channel.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <optional>
#include <thread>
#include <vector>

namespace {

using Values = std::vector<int>;

Values iota(int first, int count) {
    Values values;
    for (int i = first; i < first + count; ++i) {
        values.push_back(i);
    }
    return values;
}

/// Runs a test body, a coroutine that starts its senders and receivers itself, to completion.
void run(coro::Task<void> body) {
    coro::SerialExecutor::create()->syncWait(std::move(body));
}

/// Sends `values` in order, sleeping `delayMs` before each send. `sent` counts the sends that completed.
coro::Task<void>
sendEach(coro::BoundedChannel<int> channel, Values values, std::atomic<int>& sent, uint32_t delayMs = 0) {
    for (int value : values) {
        if (delayMs > 0) {
            co_await coro::sleep(delayMs);
        }
        const bool ok = co_await channel.send(value);
        EXPECT_TRUE(ok);
        ++sent;
    }
}

coro::Task<void> sendEachAndClose(coro::BoundedChannel<int> channel, Values values, std::atomic<int>& sent) {
    co_await sendEach(channel, std::move(values), sent);
    channel.close();
}

coro::Task<bool> sendAfter(coro::BoundedChannel<int> channel, int value, uint32_t delayMs) {
    if (delayMs > 0) {
        co_await coro::sleep(delayMs);
    }
    co_return co_await channel.send(value);
}

/// Receives until the channel ends, sleeping `delayMs` before each receive, and appends the values to `received`.
template <typename Channel>
coro::Task<void> receiveEach(Channel channel, Values& received, uint32_t delayMs = 0) {
    while (true) {
        if (delayMs > 0) {
            co_await coro::sleep(delayMs);
        }
        auto value = co_await channel.receive();
        if (!value) {
            co_return;
        }
        received.push_back(*value);
    }
}

template <typename Channel>
coro::Task<std::optional<int>> receiveAfter(Channel channel, uint32_t delayMs) {
    if (delayMs > 0) {
        co_await coro::sleep(delayMs);
    }
    co_return co_await channel.receive();
}

template <typename Channel>
coro::Task<void> closeAfter(Channel channel, uint32_t delayMs) {
    co_await coro::sleep(delayMs);
    channel.close();
}

/// Awaits a task that was scheduled with its own stop token: true if it ended with StopError.
template <typename T>
coro::Task<bool> endsStopped(coro::Task<T> task) {
    try {
        co_await task;
    } catch (const coro::StopError&) {
        co_return true;
    }
    co_return false;
}

} // namespace

TEST(BoundedChannel, KeepsFifoOrder) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {2};
        std::atomic<int> sent = 0;
        Values received;
        // The consumer waits before every receive, so the producer keeps running into a full channel.
        co_await coro::all(sendEachAndClose(channel, iota(0, 20), sent), receiveEach(channel, received, 2));
        EXPECT_EQ(received, iota(0, 20));
        EXPECT_EQ(sent, 20);
    }());
}

TEST(BoundedChannel, SendWaitsWhileFull) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {2};
        std::atomic<int> sent = 0;
        auto executor = co_await coro::currentExecutor;
        auto producing = executor->schedule(sendEachAndClose(channel, iota(0, 5), sent));

        co_await coro::sleep(50);
        // Two values fill the channel; the third send waits for room.
        EXPECT_EQ(sent, 2);
        EXPECT_FALSE(producing.ready());

        auto first = co_await channel.receive();
        EXPECT_EQ(first, 0);
        co_await coro::sleep(20);
        // Taking one value made room for exactly one more.
        EXPECT_EQ(sent, 3);

        Values rest;
        co_await receiveEach(channel, rest);
        EXPECT_EQ(rest, iota(1, 4));
        co_await producing;
        EXPECT_EQ(sent, 5);
    }());
}

TEST(BoundedChannel, ZeroCapacitySendWaitsForAReceiver) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {0};
        std::atomic<int> sent = 0;
        auto executor = co_await coro::currentExecutor;
        auto producing = executor->schedule(sendEach(channel, {7}, sent));

        co_await coro::sleep(50);
        // Nothing can hold the value but a receiver.
        EXPECT_EQ(sent, 0);

        auto value = co_await channel.receive();
        EXPECT_EQ(value, 7);
        co_await producing;
        EXPECT_EQ(sent, 1);
    }());
}

TEST(BoundedChannel, ZeroCapacityReceiveWaitsForASender) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {0};
        auto other = coro::SerialExecutor::create();
        auto sending = other->schedule(sendAfter(channel, 7, 50));

        auto value = co_await channel.receive();
        EXPECT_EQ(value, 7);
        const bool sent = co_await sending;
        EXPECT_TRUE(sent);
    }());
}

TEST(BoundedChannel, CloseEndsWaitingReceivers) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {1};
        auto executor = co_await coro::currentExecutor;
        auto other = coro::SerialExecutor::create();
        auto here = executor->schedule(receiveAfter(channel, 0));
        auto there = other->schedule(receiveAfter(channel, 0));
        auto closing = other->schedule(closeAfter(channel, 50));

        auto hereValue = co_await here;
        auto thereValue = co_await there;
        EXPECT_EQ(hereValue, std::nullopt);
        EXPECT_EQ(thereValue, std::nullopt);
        co_await closing;
    }());
}

TEST(BoundedChannel, CloseFailsWaitingSenders) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {0};
        auto executor = co_await coro::currentExecutor;
        auto other = coro::SerialExecutor::create();
        auto here = executor->schedule(sendAfter(channel, 1, 0));
        auto there = other->schedule(sendAfter(channel, 2, 0));
        auto closing = other->schedule(closeAfter(channel, 50));

        const bool hereSent = co_await here;
        const bool thereSent = co_await there;
        EXPECT_FALSE(hereSent);
        EXPECT_FALSE(thereSent);
        co_await closing;

        const bool lateSent = co_await channel.send(3);
        EXPECT_FALSE(lateSent);
    }());
}

TEST(BoundedChannel, SlowReaderDrainsQueuedValuesAfterClose) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {3};
        std::atomic<int> sent = 0;
        auto executor = co_await coro::currentExecutor;
        // The producer queues three values and closes the channel long before the reader takes the first one.
        auto producing = executor->schedule(sendEachAndClose(channel, {1, 2, 3}, sent));

        Values received;
        co_await receiveEach(channel, received, 20);
        EXPECT_TRUE(producing.ready());
        EXPECT_EQ(received, (Values {1, 2, 3}));

        // The channel stays ended for every later reader and sender.
        auto late = co_await channel.receive();
        EXPECT_EQ(late, std::nullopt);
        const bool lateSent = co_await channel.send(4);
        EXPECT_FALSE(lateSent);
    }());
}

TEST(BoundedChannel, EachReaderTakesAQueuedValueThenTheRestSeeTheClose) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {3};
        std::atomic<int> sent = 0;
        auto executor = co_await coro::currentExecutor;
        auto producing = executor->schedule(sendEachAndClose(channel, {1, 2, 3}, sent));

        // Five readers arrive one after another, after the channel was closed with three values in it.
        auto results = co_await coro::all(receiveAfter(channel, 10),
                                          receiveAfter(channel, 20),
                                          receiveAfter(channel, 30),
                                          receiveAfter(channel, 40),
                                          receiveAfter(channel, 50));
        EXPECT_EQ(results, (std::vector<std::optional<int>> {1, 2, 3, std::nullopt, std::nullopt}));
        co_await producing;
    }());
}

TEST(BoundedChannel, CloseWithAWaitingSenderKeepsQueuedValues) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {2};
        auto executor = co_await coro::currentExecutor;
        // Two values fill the channel and the third sender waits for room.
        auto first = executor->schedule(sendAfter(channel, 1, 0));
        auto second = executor->schedule(sendAfter(channel, 2, 0));
        auto third = executor->schedule(sendAfter(channel, 3, 0));
        auto closing = executor->schedule(closeAfter(channel, 50));

        // The waiting sender's value never got in, so it is dropped; the queued ones stay for the reader.
        const bool thirdSent = co_await third;
        EXPECT_FALSE(thirdSent);
        Values received;
        co_await receiveEach(channel, received);
        EXPECT_EQ(received, (Values {1, 2}));

        const bool firstSent = co_await first;
        const bool secondSent = co_await second;
        EXPECT_TRUE(firstSent);
        EXPECT_TRUE(secondSent);
        co_await closing;
    }());
}

TEST(BoundedChannel, ProducersAndConsumersOnSeparateExecutors) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {4};
        constexpr int kProducers = 3;
        constexpr int kPerProducer = 1000;
        std::atomic<int> sent = 0;

        std::vector<coro::SerialExecutor::Ref> executors;
        std::vector<coro::Task<void>> producers;
        for (int p = 0; p < kProducers; ++p) {
            executors.push_back(coro::SerialExecutor::create());
            producers.push_back(
                executors.back()->schedule(sendEach(channel, iota(p * kPerProducer, kPerProducer), sent)));
        }
        Values first;
        Values second;
        executors.push_back(coro::SerialExecutor::create());
        auto firstConsumer = executors.back()->schedule(receiveEach(channel, first));
        executors.push_back(coro::SerialExecutor::create());
        auto secondConsumer = executors.back()->schedule(receiveEach(channel, second));

        for (auto& producer : producers) {
            co_await producer;
        }
        channel.close();
        co_await firstConsumer;
        co_await secondConsumer;
        EXPECT_EQ(sent, kProducers * kPerProducer);

        // Each consumer sees every producer's values in the order they were sent.
        for (const Values* received : {&first, &second}) {
            for (int p = 0; p < kProducers; ++p) {
                Values fromProducer;
                std::copy_if(received->begin(), received->end(), std::back_inserter(fromProducer), [&](int value) {
                    return value / kPerProducer == p;
                });
                EXPECT_TRUE(std::is_sorted(fromProducer.begin(), fromProducer.end()));
            }
        }
        // And every value reached exactly one consumer.
        Values all = first;
        all.insert(all.end(), second.begin(), second.end());
        std::sort(all.begin(), all.end());
        EXPECT_EQ(all, iota(0, kProducers * kPerProducer));
    }());
}

TEST(BoundedChannel, StopWhileReceiving) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {1};
        auto executor = co_await coro::currentExecutor;
        coro::StopSource stop;
        auto receiving = executor->schedule(receiveAfter(channel, 0).setStopToken(stop.token()));

        co_await coro::sleep(20);
        stop.requestStop();
        const bool stopped = co_await endsStopped(receiving);
        EXPECT_TRUE(stopped);

        // The stopped receiver left the channel: the next value goes to the next receiver.
        auto next = executor->schedule(receiveAfter(channel, 0));
        const bool sent = co_await sendAfter(channel, 3, 20);
        EXPECT_TRUE(sent);
        auto value = co_await next;
        EXPECT_EQ(value, 3);
    }());
}

TEST(BoundedChannel, StopWhileSending) {
    run([]() -> coro::Task<void> {
        coro::BoundedChannel<int> channel {0};
        auto executor = co_await coro::currentExecutor;
        coro::StopSource stop;
        auto sending = executor->schedule(sendAfter(channel, 1, 0).setStopToken(stop.token()));

        co_await coro::sleep(20);
        stop.requestStop();
        const bool stopped = co_await endsStopped(sending);
        EXPECT_TRUE(stopped);

        // The stopped sender's value is gone: a receiver waits until the channel closes.
        auto closing = executor->schedule(closeAfter(channel, 20));
        auto value = co_await channel.receive();
        EXPECT_EQ(value, std::nullopt);
        co_await closing;
    }());
}

TEST(Channel, ThreadSendsCoroutineReceives) {
    run([]() -> coro::Task<void> {
        coro::Channel<int> channel;
        std::thread producer([channel] {
            for (int i = 0; i < 100; ++i) {
                EXPECT_TRUE(channel.send(i));
            }
            channel.close();
        });
        Values received;
        co_await receiveEach(channel, received);
        producer.join();
        EXPECT_EQ(received, iota(0, 100));
        EXPECT_FALSE(channel.send(100));
    }());
}

coro::Task<void> burstAndClose(coro::Channel<int> channel, const std::atomic<int>& received, int& receivedAtClose) {
    co_await coro::sleep(20);
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(channel.send(i));
    }
    receivedAtClose = received;
    channel.close();
}

template <typename Channel>
coro::Task<void> countingReceiver(Channel channel, Values& values, std::atomic<int>& received) {
    while (auto value = co_await channel.receive()) {
        values.push_back(*value);
        ++received;
        co_await coro::sleep(5);
    }
}

TEST(Channel, ReadersOnTwoExecutorsDrainQueuedValuesAfterClose) {
    run([]() -> coro::Task<void> {
        coro::Channel<int> channel;
        std::atomic<int> received = 0;
        int receivedAtClose = -1;
        Values here;
        Values there;
        // Both readers wait before anything is sent. The producer sends a burst of twenty values and closes the
        // channel right away, while the readers, which pause after every value, have taken only a few.
        auto other = coro::SerialExecutor::create();
        auto remote = other->schedule(countingReceiver(channel, there, received));
        auto producer = coro::SerialExecutor::create();
        auto producing = producer->schedule(burstAndClose(channel, received, receivedAtClose));
        co_await countingReceiver(channel, here, received);
        co_await remote;
        co_await producing;

        EXPECT_LT(receivedAtClose, 20);
        EXPECT_TRUE(std::is_sorted(here.begin(), here.end()));
        EXPECT_TRUE(std::is_sorted(there.begin(), there.end()));
        Values all = here;
        all.insert(all.end(), there.begin(), there.end());
        std::sort(all.begin(), all.end());
        EXPECT_EQ(all, iota(0, 20));
    }());
}
