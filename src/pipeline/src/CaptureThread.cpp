#include "inspector/CaptureThread.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

namespace inspector {

namespace {
constexpr double kDefaultFps = 25.0;  // kaynak fps bildirmezse
}

CaptureThread::CaptureThread(std::unique_ptr<FrameSource> source,
                             std::shared_ptr<ThreadSafeQueue<Frame>> queue,
                             ReconnectPolicy policy, QObject* parent)
    : QThread(parent), m_source(std::move(source)), m_queue(std::move(queue)), m_policy(policy)
{
}

CaptureThread::~CaptureThread()
{
    requestInterruption();
    wait();
}

void CaptureThread::run()
{
    using Clock = std::chrono::steady_clock;

    try {
        const double fps = m_source->fps() > 0.0 ? m_source->fps() : kDefaultFps;
        const auto period =
            std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
        auto nextDeadline = Clock::now();

        while (!isInterruptionRequested()) {
            auto frame = m_source->next();
            if (!frame) {
                // Kamera koptuysa yeniden bağlanmayı dene; dosya bittiyse normal sonlan.
                if (recoverSource()) {
                    nextDeadline = Clock::now();
                    continue;
                }
                if (!isInterruptionRequested()) {
                    emit sourceExhausted();
                }
                break;
            }
            if (!m_queue->push(std::move(*frame))) {
                break;  // kuyruk kapatılmış: durdurma isteği
            }

            // Kamera kendi hızında kare verir. Dosya kaynakları ise anında okunur;
            // gerçek zamanlı görünmesi için fps'e göre bekletiyoruz.
            if (!m_source->isLive()) {
                nextDeadline += period;
                const auto now = Clock::now();
                if (nextDeadline < now) {
                    nextDeadline = now;  // geride kaldıysak telafi etmeye çalışma
                }
                std::this_thread::sleep_until(nextDeadline);
            }
        }
    } catch (const std::exception& e) {
        // İstisna thread sınırını geçemez: yakalanmazsa std::terminate çağrılır.
        emit errorOccurred(QString::fromStdString(e.what()));
    }

    // Tek üretici biz olduğumuz için kuyruğu kapatmak da bizim işimiz.
    m_queue->close();
}

bool CaptureThread::recoverSource()
{
    if (!m_source->canReopen() || isInterruptionRequested()) {
        return false;
    }
    emit sourceLost(QString::fromStdString(m_source->description()));

    // Üstel geri çekilme: 250 ms, 500 ms, 1 s ... en fazla maxDelayMs. Kullanıcı Stop'a
    // basana kadar denemeye devam eder (bir üretim hattında kamera geri gelecektir).
    int delayMs = m_policy.initialDelayMs;
    for (int attempt = 1;; ++attempt) {
        if (!sleepInterruptibly(delayMs)) {
            return false;  // Stop istendi
        }
        if (m_source->reopen()) {
            emit sourceRecovered(attempt);
            return true;
        }
        delayMs = std::min(delayMs * 2, m_policy.maxDelayMs);
    }
}

bool CaptureThread::sleepInterruptibly(int ms)
{
    // Tek parça 4 sn uyusaydık, Stop'a basıldığında kapanış 4 sn gecikirdi.
    // Küçük dilimlerle uyuyup her dilimde durdurma isteğine bakıyoruz.
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < deadline) {
        if (isInterruptionRequested()) {
            return false;
        }
        std::this_thread::sleep_for(std::min<steady_clock::duration>(
            milliseconds(20), deadline - steady_clock::now()));
    }
    return !isInterruptionRequested();
}

}  // namespace inspector
