#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace inspector {

/// Sınırlı kapasiteli, çok üreticili / çok tüketicili (MPMC) thread-safe kuyruk.
///
/// Tasarım kararları:
///  * Kuyruk doluyken push() BLOKLAMAZ, en eski elemanı atar (drop-oldest).
///    Canlı görüntüde eski kareleri işleyip gecikme biriktirmek yerine en
///    güncel kareyi işlemek tercih edilir. Atılan kare sayısı droppedCount()
///    ile izlenir (GUI'de "düşen kare" olarak gösterilecek).
///  * close() çağrıldıktan sonra push() reddedilir, ama tüketiciler kuyrukta
///    kalanları boşaltmaya devam edebilir; kuyruk boşalınca pop() std::nullopt
///    döner. Bu, thread'lerin temiz (graceful) kapanmasını sağlar.
template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t capacity) : m_capacity(capacity)
    {
        if (capacity == 0) {
            throw std::invalid_argument("ThreadSafeQueue capacity must be > 0");
        }
    }

    // Mutex kopyalanamaz/taşınamaz; kuyruğu da kopyalamak anlamsız.
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /// Elemanı ekler. Kuyruk kapalıysa false döner.
    bool push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed) {
                return false;
            }
            if (m_items.size() >= m_capacity) {
                m_items.pop_front();
                ++m_dropped;
            }
            m_items.push_back(std::move(item));
        }
        // notify kilidin DIŞINDA: uyanan thread hemen mutex'i alabilsin.
        m_notEmpty.notify_one();
        return true;
    }

    /// Eleman gelene ya da kuyruk kapanıp boşalana kadar bekler.
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // Predicate'li wait: sahte uyanmalara (spurious wakeup) karşı güvenli.
        m_notEmpty.wait(lock, [this] { return m_closed || !m_items.empty(); });
        return takeFrontLocked();
    }

    /// pop() gibi, ama en fazla `timeout` kadar bekler.
    template <typename Rep, typename Period>
    std::optional<T> popFor(const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait_for(lock, timeout, [this] { return m_closed || !m_items.empty(); });
        return takeFrontLocked();
    }

    /// Yeni eleman kabulünü durdurur ve bekleyen tüm tüketicileri uyandırır.
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_all();
    }

    bool isClosed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    std::size_t droppedCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

    std::size_t capacity() const noexcept { return m_capacity; }

private:
    // Ön koşul: m_mutex çağıran tarafından kilitlenmiş olmalı.
    std::optional<T> takeFrontLocked()
    {
        if (m_items.empty()) {
            return std::nullopt;
        }
        std::optional<T> item(std::move(m_items.front()));
        m_items.pop_front();
        return item;
    }

    mutable std::mutex m_mutex;  // const metotlarda da kilitleyebilmek için mutable
    std::condition_variable m_notEmpty;
    std::deque<T> m_items;
    const std::size_t m_capacity;
    std::size_t m_dropped = 0;
    bool m_closed = false;
};

}  // namespace inspector
