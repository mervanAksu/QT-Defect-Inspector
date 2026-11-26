#include "inspector/CaptureThread.h"

#include <chrono>
#include <exception>
#include <thread>

namespace inspector {

namespace {
constexpr double kDefaultFps = 25.0;  // kaynak fps bildirmezse
}

CaptureThread::CaptureThread(std::unique_ptr<FrameSource> source,
                             std::shared_ptr<ThreadSafeQueue<Frame>> queue, QObject* parent)
    : QThread(parent), m_source(std::move(source)), m_queue(std::move(queue))
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
                emit sourceExhausted();
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

}  // namespace inspector
