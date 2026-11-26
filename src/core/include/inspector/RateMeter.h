#pragma once

#include <chrono>
#include <deque>

namespace inspector {

/// Kayan pencere ile olay hızı (ör. FPS) ölçer: son `window` süresindeki olay sayısı.
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

    /// Saniyedeki olay sayısı.
    double rate(Clock::time_point now = Clock::now())
    {
        evictOlderThan(now - m_window);
        const double seconds = std::chrono::duration<double>(m_window).count();
        return static_cast<double>(m_ticks.size()) / seconds;
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
