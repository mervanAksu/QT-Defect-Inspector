#pragma once

#include "inspector/CaptureThread.h"
#include "inspector/DefectDetector.h"
#include "inspector/InspectionResult.h"
#include "inspector/ProcessingThread.h"

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace inspector {

/// Capture ve processing thread'lerini kuran, başlatan ve durduran koordinatör.
/// GUI (ve testler) yalnızca bu sınıfla konuşur; thread detaylarını bilmez.
///
/// Bu nesne oluşturulduğu thread'de (normalde GUI thread'i) yaşar. Tüm sinyalleri
/// o thread'de yayınlanır, dolayısıyla bağlanan slot'lar widget'lara güvenle dokunabilir.
///
///   [FrameSource] -> CaptureThread -> ThreadSafeQueue -> ProcessingThread
///                                                            | frameProcessed
///                                                            v (Qt::QueuedConnection)
///                                             InspectionPipeline::resultReady -> GUI
class InspectionPipeline : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t kQueueCapacity = 4;

    explicit InspectionPipeline(QObject* parent = nullptr);
    ~InspectionPipeline() override;

    /// Kaynağı açar ve thread'leri başlatır. Kaynak açılamazsa ya da zaten
    /// çalışıyorsa false döner (hata errorOccurred ile de bildirilir).
    bool start(const QString& sourceSpec, bool loop = true);

    /// Thread'leri durdurur ve bitmelerini bekler. Çalışmıyorsa hiçbir şey yapmaz.
    void stop();

    bool isRunning() const noexcept { return m_processing != nullptr; }

    /// Çalışırken de çağrılabilir; bir sonraki karede etkili olur.
    void setThreshold(int threshold);
    int threshold() const noexcept { return m_config.threshold; }

signals:
    void started(const QString& sourceDescription);
    void resultReady(const inspector::InspectionResult& result);
    void errorOccurred(const QString& message);
    /// Hem stop() ile hem de kaynak kendiliğinden bittiğinde (loop=false) yayınlanır.
    void stopped();

private:
    void shutdownThreads();
    void onProcessingFinished(std::uint64_t generation);

    DetectorConfig m_config;
    std::shared_ptr<ThreadSafeQueue<Frame>> m_queue;  // iki thread de kullanır
    std::unique_ptr<CaptureThread> m_capture;
    std::unique_ptr<ProcessingThread> m_processing;
    // Her start() bir "nesil" açar. Eski bir çalıştırmadan geç gelen sinyaller
    // yeni çalıştırmayı yanlışlıkla durdurmasın diye nesil numarasını kontrol ederiz.
    std::uint64_t m_generation = 0;
};

}  // namespace inspector
