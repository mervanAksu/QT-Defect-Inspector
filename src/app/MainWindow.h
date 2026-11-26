#pragma once

#include "inspector/InspectionPipeline.h"
#include "inspector/RateMeter.h"

#include <QMainWindow>

#include <cstdint>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace inspector {

class ImageView;

/// Ana pencere: kaynak seçimi, Başlat/Durdur, eşik ayarı, canlı görüntü,
/// istatistikler ve log.
///
/// Widget'lar ham işaretçiyle tutulur ama SAHİPLİK Qt'nin ebeveyn-çocuk ağacındadır:
/// ebeveyni olan bir QObject, ebeveyni silinince otomatik silinir. Bu yüzden
/// burada delete ya da unique_ptr yok.
///
/// Tüm slot'lar GUI thread'inde çalışır; worker thread'lerden gelen sonuçlar
/// InspectionPipeline'daki queued bağlantılar sayesinde buraya güvenle ulaşır.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void setSource(const QString& source);
    void setThreshold(int threshold);

public slots:
    void start();
    void stop();

private slots:
    void browseFolder();
    void browseVideo();
    void onStarted(const QString& description);
    void onStopped();
    void onResult(const inspector::InspectionResult& result);
    void onError(const QString& message);

private:
    QWidget* buildControlPanel();
    void setRunningState(bool running);
    void resetStatistics();
    void appendLog(const QString& text, const QColor& color = {});

    InspectionPipeline m_pipeline;  // üye nesne: pencereyle birlikte yaşar ve ölür
    RateMeter m_fpsMeter;
    std::uint64_t m_totalFrames = 0;
    std::uint64_t m_defectiveFrames = 0;

    ImageView* m_imageView = nullptr;
    QLineEdit* m_sourceEdit = nullptr;
    QPushButton* m_browseFolderButton = nullptr;
    QPushButton* m_browseVideoButton = nullptr;
    QCheckBox* m_loopCheck = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QSlider* m_thresholdSlider = nullptr;
    QSpinBox* m_thresholdSpin = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_latencyLabel = nullptr;
    QLabel* m_droppedLabel = nullptr;
    QLabel* m_defectsLabel = nullptr;
    QPlainTextEdit* m_logView = nullptr;
};

}  // namespace inspector
