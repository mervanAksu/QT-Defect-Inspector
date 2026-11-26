#include "inspector/InspectionPipeline.h"

#include <algorithm>
#include <exception>

namespace inspector {

InspectionPipeline::InspectionPipeline(QObject* parent) : QObject(parent)
{
    // Özel tipin queued connection ve QSignalSpy ile taşınabilmesi için.
    qRegisterMetaType<InspectionResult>();
}

InspectionPipeline::~InspectionPipeline()
{
    shutdownThreads();  // destructor'da sinyal yayınlamıyoruz
}

bool InspectionPipeline::start(const QString& sourceSpec, bool loop)
{
    if (isRunning()) {
        emit errorOccurred(tr("Pipeline is already running"));
        return false;
    }

    std::unique_ptr<FrameSource> source;
    try {
        source = createFrameSource(sourceSpec.toStdString(), loop);
    } catch (const std::exception& e) {
        emit errorOccurred(QString::fromStdString(e.what()));
        return false;
    }
    const QString description = QString::fromStdString(source->description());

    m_queue = std::make_shared<ThreadSafeQueue<Frame>>(kQueueCapacity);
    m_capture = std::make_unique<CaptureThread>(std::move(source), m_queue);
    m_processing = std::make_unique<ProcessingThread>(m_queue, m_config);
    const std::uint64_t generation = ++m_generation;

    // Sinyaller worker thread'lerinden yayınlanır, bu nesne ise GUI thread'inde yaşar.
    // QueuedConnection: argümanlar kopyalanıp GUI thread'inin olay kuyruğuna konur ve
    // orada işlenir. (Farklı thread'lerde Qt bunu AutoConnection ile de seçerdi; açıkça
    // yazmak niyeti belgeliyor.)
    connect(m_processing.get(), &ProcessingThread::frameProcessed, this,
            &InspectionPipeline::resultReady, Qt::QueuedConnection);
    connect(m_processing.get(), &ProcessingThread::errorOccurred, this,
            &InspectionPipeline::errorOccurred, Qt::QueuedConnection);
    connect(m_capture.get(), &CaptureThread::errorOccurred, this,
            &InspectionPipeline::errorOccurred, Qt::QueuedConnection);
    // Kaynak bitince capture kuyruğu kapatır, processing kalanları işleyip biter.
    connect(m_processing.get(), &QThread::finished, this,
            [this, generation] { onProcessingFinished(generation); }, Qt::QueuedConnection);

    m_processing->start();  // önce tüketici: ilk kare geldiğinde hazır beklesin
    m_capture->start();
    emit started(description);
    return true;
}

void InspectionPipeline::stop()
{
    if (!isRunning()) {
        return;
    }
    shutdownThreads();
    emit stopped();
}

void InspectionPipeline::setThreshold(int threshold)
{
    m_config.threshold = std::clamp(threshold, 0, 255);
    if (m_processing) {
        m_processing->setThreshold(m_config.threshold);
    }
}

void InspectionPipeline::shutdownThreads()
{
    if (!isRunning()) {
        return;
    }
    // Önce bağlantıları kes: durdururken gelecek "finished" sinyali
    // onProcessingFinished'i tekrar tetiklemesin.
    m_capture->disconnect(this);
    m_processing->disconnect(this);

    m_capture->requestInterruption();
    m_processing->requestInterruption();
    m_queue->close();  // pop()'ta bekleyen processing thread'ini uyandır

    m_capture->wait();
    m_processing->wait();

    m_capture.reset();
    m_processing.reset();
    m_queue.reset();
}

void InspectionPipeline::onProcessingFinished(std::uint64_t generation)
{
    if (generation != m_generation || !isRunning()) {
        return;  // eski bir çalıştırmadan gecikmeli gelen sinyal
    }
    stop();
}

}  // namespace inspector
