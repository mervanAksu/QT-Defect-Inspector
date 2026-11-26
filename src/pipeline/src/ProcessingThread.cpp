#include "inspector/ProcessingThread.h"

#include "inspector/ImageConversion.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace inspector {

ProcessingThread::ProcessingThread(std::shared_ptr<ThreadSafeQueue<Frame>> queue,
                                   DetectorConfig config, QObject* parent)
    : QThread(parent),
      m_queue(std::move(queue)),
      m_initialConfig(config),
      m_threshold(config.threshold)
{
}

ProcessingThread::~ProcessingThread()
{
    requestInterruption();
    m_queue->close();  // pop()'ta bekliyorsa uyandır
    wait();
}

void ProcessingThread::setThreshold(int threshold)
{
    m_threshold.store(std::clamp(threshold, 0, 255));
}

int ProcessingThread::threshold() const
{
    return m_threshold.load();
}

void ProcessingThread::run()
{
    DefectDetector detector(m_initialConfig);

    while (!isInterruptionRequested()) {
        auto frame = m_queue->pop();  // kare yoksa burada uyur (CPU harcamaz)
        if (!frame) {
            break;  // kuyruk kapandı ve boşaldı
        }

        try {
            // Eşik değiştiyse dedektörü güncelle. Dedektöre sadece bu thread dokunur.
            const int threshold = m_threshold.load();
            if (threshold != detector.config().threshold) {
                DetectorConfig config = detector.config();
                config.threshold = threshold;
                detector.setConfig(config);
            }

            InspectionResult result;
            result.frameIndex = frame->index;
            result.detection = detector.detect(frame->image);

            // Kare kuyruktan bize TAŞINDI (move); başka kimse kullanmıyor, bu yüzden
            // kopyalamadan doğrudan üzerine çizebiliriz.
            DefectDetector::drawDefects(frame->image, result.detection);
            result.annotatedImage = matToQImage(frame->image);

            const auto latency = std::chrono::steady_clock::now() - frame->timestamp;
            result.latencyMs = std::chrono::duration<double, std::milli>(latency).count();
            result.droppedFrames = m_queue->droppedCount();

            // Bu thread'den emit edilir; GUI'deki slot, QueuedConnection sayesinde
            // GUI thread'inde çalışır.
            emit frameProcessed(result);
        } catch (const std::exception& e) {
            emit errorOccurred(QString::fromStdString(e.what()));  // tek kare hatası akışı durdurmaz
        }
    }
}

}  // namespace inspector
