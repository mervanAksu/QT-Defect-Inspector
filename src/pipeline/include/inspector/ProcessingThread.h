#pragma once

#include "inspector/DefectDetector.h"
#include "inspector/FrameSource.h"
#include "inspector/InspectionResult.h"
#include "inspector/ThreadSafeQueue.h"

#include <QThread>

#include <atomic>
#include <memory>

namespace inspector {

/// Tüketici (consumer): kuyruktan kare alır, kusur tespiti yapar ve sonucu
/// frameProcessed sinyaliyle yayınlar.
///
/// Eşik değeri GUI thread'inden değiştirilir ama bu thread'de okunur. Tek bir int
/// olduğu için std::atomic yeterli: kilit gerekmez ve data race oluşmaz.
/// DefectDetector nesnesi ise yalnızca run() içinde, yani tek bir thread'de yaşar;
/// böylece onu hiç paylaşmak zorunda kalmayız ("paylaşma, sahiplen").
class ProcessingThread : public QThread {
    Q_OBJECT

public:
    ProcessingThread(std::shared_ptr<ThreadSafeQueue<Frame>> queue, DetectorConfig config,
                     QObject* parent = nullptr);
    ~ProcessingThread() override;

    /// Her thread'den güvenle çağrılabilir. Değer [0, 255] aralığına kırpılır.
    void setThreshold(int threshold);
    int threshold() const;

signals:
    void frameProcessed(const inspector::InspectionResult& result);
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    std::shared_ptr<ThreadSafeQueue<Frame>> m_queue;
    const DetectorConfig m_initialConfig;
    std::atomic<int> m_threshold;
};

}  // namespace inspector
