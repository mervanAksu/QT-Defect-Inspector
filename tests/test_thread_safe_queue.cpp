#include "inspector/ThreadSafeQueue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using inspector::ThreadSafeQueue;
using namespace std::chrono_literals;

TEST(ThreadSafeQueueTest, ZeroCapacityThrows)
{
    EXPECT_THROW({ ThreadSafeQueue<int> q(0); }, std::invalid_argument);
}

TEST(ThreadSafeQueueTest, PreservesFifoOrder)
{
    ThreadSafeQueue<int> q(10);
    q.push(1);
    q.push(2);
    q.push(3);

    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), 3);
    EXPECT_EQ(q.size(), 0u);
}

TEST(ThreadSafeQueueTest, DropsOldestWhenFull)
{
    ThreadSafeQueue<int> q(2);
    q.push(1);
    q.push(2);
    q.push(3);  // 1 atılmalı

    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.droppedCount(), 1u);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), 3);
}

TEST(ThreadSafeQueueTest, PopForTimesOutOnEmptyQueue)
{
    ThreadSafeQueue<int> q(1);
    const auto start = std::chrono::steady_clock::now();

    EXPECT_EQ(q.popFor(20ms), std::nullopt);
    EXPECT_GE(std::chrono::steady_clock::now() - start, 20ms);
}

TEST(ThreadSafeQueueTest, CloseRejectsPushButDrainsRemainingItems)
{
    ThreadSafeQueue<int> q(5);
    q.push(1);
    q.push(2);
    q.close();

    EXPECT_FALSE(q.push(3));
    EXPECT_TRUE(q.isClosed());
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), std::nullopt);  // kapalı + boş -> bloklamadan döner
}

TEST(ThreadSafeQueueTest, CloseWakesUpBlockedConsumer)
{
    ThreadSafeQueue<int> q(1);
    std::optional<int> result = 42;

    std::thread consumer([&] { result = q.pop(); });  // boş kuyrukta bloklanır
    std::this_thread::sleep_for(50ms);
    q.close();
    consumer.join();  // close() uyandırmasaydı test burada asılı kalırdı

    EXPECT_EQ(result, std::nullopt);
}

TEST(ThreadSafeQueueTest, MultipleProducersAndConsumersLoseNothing)
{
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kItemsPerProducer = 10'000;

    // Kapasite toplam eleman sayısı kadar: hiçbir şey düşmemeli.
    ThreadSafeQueue<int> q(kProducers * kItemsPerProducer);
    std::atomic<long long> sum{0};
    std::atomic<int> count{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (auto item = q.pop()) {
                sum += *item;
                ++count;
            }
        });
    }

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 1; i <= kItemsPerProducer; ++i) {
                q.push(i);
            }
        });
    }

    for (auto& t : producers) t.join();
    q.close();  // tüketiciler kalanları boşaltıp çıkacak
    for (auto& t : consumers) t.join();

    const long long expectedSum =
        static_cast<long long>(kProducers) * kItemsPerProducer * (kItemsPerProducer + 1) / 2;
    EXPECT_EQ(count.load(), kProducers * kItemsPerProducer);
    EXPECT_EQ(sum.load(), expectedSum);
    EXPECT_EQ(q.droppedCount(), 0u);
}
