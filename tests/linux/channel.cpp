#include <coro/coro.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/all.hpp>
#include <coro/sleep.hpp>
#include <coro/sync/channel.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <numeric>
#include <optional>
#include <thread>
#include <vector>

// Every test is a job, as a channel would be used for real: a producer sends values into the channel and closes it
// when it is done, while a consumer takes values until the channel ends.

namespace {

constexpr int sumTo(int n) {
    return n * (n + 1) / 2;
}

template <typename R>
R run(coro::Task<R> job) {
    return coro::SerialExecutor::create()->syncWait(std::move(job));
}

} // namespace

coro::Task<int> sumThroughChannel() {
    coro::BoundedChannel<int> channel {10};

    auto producer = [](coro::BoundedChannel<int> channel) -> coro::Task<void> {
        for (int i = 1; i <= 100; ++i) {
            co_await channel.send(i);
        }
        channel.close();
    };

    auto consumer = [](coro::BoundedChannel<int> channel) -> coro::Task<int> {
        int sum = 0;
        int previous = 0;
        while (auto value = co_await channel.receive()) {
            EXPECT_EQ(*value, previous + 1);
            previous = *value;
            sum += *value;
        }
        co_return sum;
    };

    (co_await coro::currentExecutor)->schedule(producer(channel));
    co_return co_await consumer(channel);
}

TEST(BoundedChannel, ProducerAndConsumer) {
    EXPECT_EQ(run(sumThroughChannel()), sumTo(100));
}

namespace {
constexpr int kSlowConsumerValues = 20;
}

/// The consumer pauses before every receive, so the producer keeps filling the channel and waiting for room.
coro::Task<int> slowConsumer(int capacity) {
    coro::BoundedChannel<int> channel {static_cast<std::size_t>(capacity)};
    int sent = 0;

    auto producer = [](coro::BoundedChannel<int> channel, int& sent) -> coro::Task<void> {
        for (int i = 1; i <= kSlowConsumerValues; ++i) {
            co_await channel.send(i);
            ++sent;
        }
        channel.close();
    };

    auto consumer = [](coro::BoundedChannel<int> channel, const int& sent, int capacity) -> coro::Task<int> {
        int sum = 0;
        int received = 0;
        while (true) {
            co_await coro::sleep(2);
            // The producer used the pause to fill the channel, and got no further.
            EXPECT_EQ(sent, std::min(received + capacity, kSlowConsumerValues));
            auto value = co_await channel.receive();
            if (!value) {
                break;
            }
            ++received;
            sum += *value;
        }
        co_return sum;
    };

    auto producing = (co_await coro::currentExecutor)->schedule(producer(channel, sent));
    const int sum = co_await consumer(channel, sent, capacity);
    co_await producing;
    co_return sum;
}

TEST(BoundedChannel, SlowConsumerHoldsTheProducerBack) {
    // Capacity 0 is a rendezvous: each send completes only once the consumer takes its value.
    for (int capacity : {0, 1, 4}) {
        SCOPED_TRACE(capacity);
        EXPECT_EQ(run(slowConsumer(capacity)), sumTo(kSlowConsumerValues));
    }
}

coro::Task<int> slowProducer() {
    coro::BoundedChannel<int> channel {10};

    auto producer = [](coro::BoundedChannel<int> channel) -> coro::Task<void> {
        for (int i = 1; i <= 10; ++i) {
            co_await coro::sleep(2);
            co_await channel.send(i);
        }
        // The consumer is waiting for the next value by now; closing ends its wait.
        co_await coro::sleep(10);
        channel.close();
    };

    auto consumer = [](coro::BoundedChannel<int> channel) -> coro::Task<int> {
        int sum = 0;
        while (auto value = co_await channel.receive()) {
            sum += *value;
        }
        co_return sum;
    };

    (co_await coro::currentExecutor)->schedule(producer(channel));
    co_return co_await consumer(channel);
}

TEST(BoundedChannel, ConsumerWaitsForASlowProducer) {
    EXPECT_EQ(run(slowProducer()), sumTo(10));
}

coro::Task<int> producerClosesFirst() {
    coro::BoundedChannel<int> channel {10};
    bool closed = false;

    auto producer = [](coro::BoundedChannel<int> channel, bool& closed) -> coro::Task<void> {
        for (int i = 1; i <= 10; ++i) {
            co_await channel.send(i);
        }
        channel.close();
        closed = true;
    };

    auto consumer = [](coro::BoundedChannel<int> channel, const bool& closed) -> coro::Task<int> {
        int sum = 0;
        while (true) {
            co_await coro::sleep(2);
            // The producer is long done, yet every value it queued before closing is still delivered.
            EXPECT_TRUE(closed);
            auto value = co_await channel.receive();
            if (!value) {
                break;
            }
            sum += *value;
        }
        co_return sum;
    };

    (co_await coro::currentExecutor)->schedule(producer(channel, closed));
    const int sum = co_await consumer(channel, closed);

    // Once the queued values are gone, every later receive sees the end of the channel and every send fails.
    auto late = co_await channel.receive();
    EXPECT_EQ(late, std::nullopt);
    const bool lateSent = co_await channel.send(11);
    EXPECT_FALSE(lateSent);
    co_return sum;
}

TEST(BoundedChannel, ConsumerDrainsWhatWasQueuedBeforeClose) {
    EXPECT_EQ(run(producerClosesFirst()), sumTo(10));
}

coro::Task<std::vector<int>> fanOut() {
    coro::BoundedChannel<int> channel {4};

    auto producer = [](coro::BoundedChannel<int> channel) -> coro::Task<void> {
        for (int i = 1; i <= 100; ++i) {
            co_await channel.send(i);
        }
        channel.close();
    };

    auto consumer = [](coro::BoundedChannel<int> channel) -> coro::Task<int> {
        int sum = 0;
        int previous = 0;
        while (auto value = co_await channel.receive()) {
            // Values reach each consumer in the order they were sent.
            EXPECT_GT(*value, previous);
            previous = *value;
            sum += *value;
            co_await coro::sleep(1);
        }
        co_return sum;
    };

    (co_await coro::currentExecutor)->schedule(producer(channel));
    std::vector<coro::Task<int>> consumers;
    for (int i = 0; i < 3; ++i) {
        consumers.push_back(coro::SerialExecutor::create()->schedule(consumer(channel)));
    }
    co_return co_await coro::all(std::move(consumers));
}

TEST(BoundedChannel, ConsumersOnSeparateExecutorsShareTheWork) {
    const auto sums = run(fanOut());
    // Every value reached exactly one consumer, and every consumer got some.
    EXPECT_EQ(std::accumulate(sums.begin(), sums.end(), 0), sumTo(100));
    for (int sum : sums) {
        EXPECT_GT(sum, 0);
    }
}

namespace {
constexpr int kFanInProducers = 3;
constexpr int kFanInValues = 1000;
} // namespace

coro::Task<int> fanIn() {
    coro::BoundedChannel<int> channel {4};

    auto producer = [](coro::BoundedChannel<int> channel, int first) -> coro::Task<void> {
        for (int i = first; i < first + kFanInValues; ++i) {
            co_await channel.send(i);
        }
    };

    // No single producer knows when the channel is done; whoever started them closes it once all of them are.
    auto closeWhenDone = [](coro::BoundedChannel<int> channel,
                            std::vector<coro::Task<void>> producers) -> coro::Task<void> {
        co_await coro::all(std::move(producers));
        channel.close();
    };

    auto consumer = [](coro::BoundedChannel<int> channel) -> coro::Task<int> {
        int sum = 0;
        std::vector<int> previous(kFanInProducers, 0);
        while (auto value = co_await channel.receive()) {
            // Each producer's values arrive in the order it sent them.
            const int producer = (*value - 1) / kFanInValues;
            EXPECT_GT(*value, previous[producer]);
            previous[producer] = *value;
            sum += *value;
        }
        co_return sum;
    };

    std::vector<coro::Task<void>> producers;
    for (int p = 0; p < kFanInProducers; ++p) {
        producers.push_back(coro::SerialExecutor::create()->schedule(producer(channel, 1 + p * kFanInValues)));
    }
    (co_await coro::currentExecutor)->schedule(closeWhenDone(channel, std::move(producers)));
    co_return co_await consumer(channel);
}

TEST(BoundedChannel, ProducersOnSeparateExecutorsShareTheChannel) {
    EXPECT_EQ(run(fanIn()), sumTo(kFanInProducers * kFanInValues));
}

coro::Task<int> pipeline() {
    coro::BoundedChannel<int> numbers {4};
    coro::BoundedChannel<int> squares {4};

    auto generate = [](coro::BoundedChannel<int> out) -> coro::Task<void> {
        for (int i = 1; i <= 100; ++i) {
            co_await out.send(i);
        }
        out.close();
    };

    // A stage in the middle consumes one channel and produces into the next, and passes the end along.
    auto square = [](coro::BoundedChannel<int> in, coro::BoundedChannel<int> out) -> coro::Task<void> {
        while (auto value = co_await in.receive()) {
            co_await out.send(*value * *value);
        }
        out.close();
    };

    auto sum = [](coro::BoundedChannel<int> in) -> coro::Task<int> {
        int sum = 0;
        while (auto value = co_await in.receive()) {
            sum += *value;
        }
        co_return sum;
    };

    coro::SerialExecutor::create()->schedule(generate(numbers));
    coro::SerialExecutor::create()->schedule(square(numbers, squares));
    co_return co_await sum(squares);
}

TEST(BoundedChannel, PipelineOfStagesOnSeparateExecutors) {
    EXPECT_EQ(run(pipeline()), 100 * 101 * 201 / 6);
}

namespace {
constexpr int kEarlyStopCapacity = 4;
}

coro::Task<void> consumerStopsEarly() {
    coro::BoundedChannel<int> channel {kEarlyStopCapacity};

    // Produces for as long as anyone listens: a failed send means the channel was closed.
    auto producer = [](coro::BoundedChannel<int> channel) -> coro::Task<int> {
        int sent = 0;
        while (co_await channel.send(sent + 1)) {
            ++sent;
        }
        co_return sent;
    };

    // Takes what it needs and closes the channel to tell the producer to stop.
    auto consumer = [](coro::BoundedChannel<int> channel) -> coro::Task<int> {
        int sum = 0;
        while (auto value = co_await channel.receive()) {
            sum += *value;
            if (*value == 10) {
                break;
            }
        }
        channel.close();
        co_return sum;
    };

    auto producing = (co_await coro::currentExecutor)->schedule(producer(channel));
    const int sum = co_await consumer(channel);
    const int sent = co_await producing;
    EXPECT_EQ(sum, sumTo(10));
    // The producer ran ahead by at most the channel's capacity, and stopped at the close.
    EXPECT_GE(sent, 10);
    EXPECT_LE(sent, 10 + kEarlyStopCapacity);
}

TEST(BoundedChannel, ConsumerClosesToStopTheProducer) {
    run(consumerStopsEarly());
}

coro::Task<void> replaceStoppedConsumer() {
    coro::BoundedChannel<int> channel {4};

    auto producer = [](coro::BoundedChannel<int> channel) -> coro::Task<void> {
        for (int i = 1; i <= 10; ++i) {
            co_await coro::sleep(5);
            co_await channel.send(i);
        }
        channel.close();
    };

    auto consumer = [](coro::BoundedChannel<int> channel, std::vector<int>& received) -> coro::Task<void> {
        while (auto value = co_await channel.receive()) {
            received.push_back(*value);
        }
    };

    auto executor = co_await coro::currentExecutor;
    coro::StopSource stop;
    std::vector<int> first;
    std::vector<int> second;
    auto producing = executor->schedule(producer(channel));
    auto consuming = executor->schedule(consumer(channel, first).setStopToken(stop.token()));

    // Stop the consumer while it waits for the producer's next value, and let another one take over.
    co_await coro::sleep(22);
    stop.requestStop();
    bool stopped = false;
    try {
        co_await consuming;
    } catch (const coro::StopError&) {
        stopped = true;
    }
    EXPECT_TRUE(stopped);
    co_await consumer(channel, second);
    co_await producing;

    // The stopped consumer left the channel: no value went to it after the stop.
    EXPECT_FALSE(first.empty());
    EXPECT_FALSE(second.empty());
    std::vector<int> all = first;
    all.insert(all.end(), second.begin(), second.end());
    EXPECT_EQ(all, (std::vector<int> {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
}

TEST(BoundedChannel, StoppedConsumerIsReplaced) {
    run(replaceStoppedConsumer());
}

coro::Task<std::vector<int>> stopTheProducer() {
    coro::BoundedChannel<int> channel {2};

    auto producer = [](coro::BoundedChannel<int> channel) -> coro::Task<void> {
        int next = 1;
        while (co_await channel.send(next++)) {
        }
    };

    auto consumer = [](coro::BoundedChannel<int> channel) -> coro::Task<std::vector<int>> {
        // Starts late, after the producer has filled the channel and been stopped.
        co_await coro::sleep(30);
        std::vector<int> received;
        while (auto value = co_await channel.receive()) {
            received.push_back(*value);
        }
        co_return received;
    };

    auto executor = co_await coro::currentExecutor;
    coro::StopSource stop;
    auto producing = executor->schedule(producer(channel).setStopToken(stop.token()));
    auto consuming = executor->schedule(consumer(channel));

    // The producer is waiting for room for its third value.
    co_await coro::sleep(10);
    stop.requestStop();
    bool stopped = false;
    try {
        co_await producing;
    } catch (const coro::StopError&) {
        stopped = true;
    }
    EXPECT_TRUE(stopped);
    // The producer cannot close the channel any more, so its owner does.
    channel.close();
    co_return co_await consuming;
}

TEST(BoundedChannel, StoppedProducerKeepsWhatItQueued) {
    // The stopped send's value never got in; the two queued before it still reach the consumer.
    EXPECT_EQ(run(stopTheProducer()), (std::vector<int> {1, 2}));
}

coro::Task<int> threadProducer() {
    coro::Channel<int> channel;

    // A plain thread, such as a callback from a library that knows nothing about coroutines, feeds the channel.
    std::thread producer([channel] {
        for (int i = 1; i <= 100; ++i) {
            EXPECT_TRUE(channel.send(i));
        }
        channel.close();
    });

    auto consumer = [](coro::Channel<int> channel) -> coro::Task<int> {
        int sum = 0;
        while (auto value = co_await channel.receive()) {
            sum += *value;
        }
        co_return sum;
    };

    const int sum = co_await consumer(channel);
    producer.join();
    EXPECT_FALSE(channel.send(101));
    co_return sum;
}

TEST(Channel, ThreadProducesCoroutineConsumes) {
    EXPECT_EQ(run(threadProducer()), sumTo(100));
}

coro::Task<std::vector<int>> burstThenClose() {
    coro::Channel<int> channel;
    std::atomic<int> received = 0;
    int receivedAtClose = -1;

    // Sends a burst at once, which never waits on an unbounded channel, and closes right after it.
    auto producer =
        [](coro::Channel<int> channel, const std::atomic<int>& received, int& receivedAtClose) -> coro::Task<void> {
        co_await coro::sleep(20);
        for (int i = 1; i <= 20; ++i) {
            channel.send(i);
        }
        receivedAtClose = received;
        channel.close();
    };

    // Pauses after every value, so most of the burst is still queued when the channel closes.
    auto consumer = [](coro::Channel<int> channel, std::atomic<int>& received) -> coro::Task<int> {
        int sum = 0;
        int previous = 0;
        while (auto value = co_await channel.receive()) {
            EXPECT_GT(*value, previous);
            previous = *value;
            sum += *value;
            ++received;
            co_await coro::sleep(5);
        }
        co_return sum;
    };

    auto producing = coro::SerialExecutor::create()->schedule(producer(channel, received, receivedAtClose));
    auto sums = co_await coro::all(coro::SerialExecutor::create()->schedule(consumer(channel, received)),
                                   coro::SerialExecutor::create()->schedule(consumer(channel, received)));
    co_await producing;
    EXPECT_LT(receivedAtClose, 20);
    co_return sums;
}

TEST(Channel, ConsumersDrainABurstAfterClose) {
    const auto sums = run(burstThenClose());
    EXPECT_EQ(sums[0] + sums[1], sumTo(20));
}
