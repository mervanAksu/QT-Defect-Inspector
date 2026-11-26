#pragma once

#include <chrono>
#include <deque>

namespace inspector {

/// Kayan pencere ile olay hızı (ör. FPS) ölçer.
///
/// Hız = (n - 1) / (en yeni - en eski olay zamanı), yalnızca son `window` içindeki
/// olaylar üzerinden. "Penceredeki olay sayısı / pencere süresi" yaklaşımı pencere
/// kenarlarına denk gelen olay yüzünden ±1 yanılır (5 FPS'i 6.0 gösterebilir);
/// olaylar arası süreyi ölçmek bu hatayı ortadan kaldırır.
///
/// Thread-safe DEĞİL; tek bir thread'den (GUI) kullanılmak üzere tasarlandı.
/// Zaman parametre olarak verilebildiği için gerçek saat beklemeden test edilebilir.
class RateMeter {
public:
    using Clock = std::chrono::steady_clock;

    explicit RateMeter(Clock::duration window = std::chrono::seconds(1)) : m_window(window) {}

    void tick(Clock::time_point now = Clock::now())
    {
        m_ticks.push_back(now);
        evictOlderThan(now - m_window);
    }

    /// Saniyedeki olay sayısı. Pencerede 2'den az olay varsa 0.
    double rate(Clock::time_point now = Clock::now())
    {
        evictOlderThan(now - m_window);
        if (m_ticks.size() < 2) {
            return 0.0;
        }
        const double span = std::chrono::duration<double>(m_ticks.back() - m_ticks.front()).count();
        return span > 0.0 ? static_cast<double>(m_ticks.size() - 1) / span : 0.0;
    }

    void reset() { m_ticks.clear(); }

private:
    void evictOlderThan(Clock::time_point limit)
    {
        while (!m_ticks.empty() && m_ticks.front() <= limit) {
            m_ticks.pop_front();
        }
    }

    Clock::duration m_window;
    std::deque<Clock::time_point> m_ticks;
};

}  // namespace inspector
