#pragma once

#include "inspector/InspectionPipeline.h"
#include "inspector/RateMeter.h"
#include "inspector/ResultPublisher.h"

#include <QMainWindow>

#include <cstdint>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QSplitter;

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
    QString source() const;
    void setThreshold(int threshold);
    /// Komut satırından ağ yayınını açmak için; arayüzdeki kutucukları işaretler.
    void enableTcp(quint16 port);
    void enableUdp(const QString& host, quint16 port);

public slots:
    void start();
    void stop();

protected:
    /// Pencere kapanırken ayarları kaydeder (QSettings).
    void closeEvent(QCloseEvent* event) override;

private slots:
    void browseFolder();
    void browseVideo();
    void onStarted(const QString& description);
    void onStopped();
    void onResult(const inspector::InspectionResult& result);
    void onError(const QString& message);
    void onTcpToggled(bool enabled);
    void onUdpToggled(bool enabled);

private:
    QWidget* buildControlPanel();
    QWidget* buildNetworkBox();
    void updateNetworkStatus();
    void loadSettings();
    void saveSettings() const;
    void setRunningState(bool running);
    void resetStatistics();
    void appendLog(const QString& text, const QColor& color = {});

    InspectionPipeline m_pipeline;  // üye nesne: pencereyle birlikte yaşar ve ölür
    ResultPublisher m_publisher;    // sonuçları TCP/UDP ile yayınlar (GUI thread'inde)
    RateMeter m_fpsMeter;
    std::uint64_t m_totalFrames = 0;
    std::uint64_t m_defectiveFrames = 0;

    QSplitter* m_splitter = nullptr;
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
    QCheckBox* m_tcpCheck = nullptr;
    QSpinBox* m_tcpPortSpin = nullptr;
    QCheckBox* m_udpCheck = nullptr;
    QLineEdit* m_udpHostEdit = nullptr;
    QSpinBox* m_udpPortSpin = nullptr;
    QLabel* m_networkLabel = nullptr;
    QPlainTextEdit* m_logView = nullptr;
};

}  // namespace inspector
