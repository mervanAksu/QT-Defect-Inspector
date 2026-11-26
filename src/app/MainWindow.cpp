#include "MainWindow.h"

#include "ImageView.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

namespace inspector {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Defect Inspector"));

    m_imageView = new ImageView;
    m_imageView->setObjectName("imageView");

    m_logView = new QPlainTextEdit;
    m_logView->setObjectName("logView");
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(1000);  // sınırsız büyüyüp belleği doldurmasın

    // Üst: görüntü + kontrol paneli. Alt: log. Arada sürüklenebilir ayraç.
    auto* top = new QWidget;
    auto* topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addWidget(m_imageView, 1);  // stretch=1: boş alanı görüntü alır
    topLayout->addWidget(buildControlPanel());

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(top);
    splitter->addWidget(m_logView);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);  // MainWindow sahipliği devralır

    // Pipeline -> pencere. Pipeline bu (GUI) thread'inde yaşadığı için bunlar
    // doğrudan bağlantıdır; thread geçişi pipeline'ın içinde, queued olarak yapılır.
    connect(&m_pipeline, &InspectionPipeline::started, this, &MainWindow::onStarted);
    connect(&m_pipeline, &InspectionPipeline::stopped, this, &MainWindow::onStopped);
    connect(&m_pipeline, &InspectionPipeline::resultReady, this, &MainWindow::onResult);
    connect(&m_pipeline, &InspectionPipeline::errorOccurred, this, &MainWindow::onError);

    // Her sonuç ağa da gider. Pipeline ve publisher aynı (GUI) thread'inde: doğrudan bağlantı.
    connect(&m_pipeline, &InspectionPipeline::resultReady, &m_publisher,
            &ResultPublisher::publish);
    connect(&m_publisher, &ResultPublisher::errorOccurred, this, &MainWindow::onError);
    connect(&m_publisher, &ResultPublisher::clientConnected, this, [this](const QString& peer) {
        appendLog(tr("TCP client connected: %1").arg(peer));
        updateNetworkStatus();
    });
    connect(&m_publisher, &ResultPublisher::clientDisconnected, this, [this](const QString& peer) {
        appendLog(tr("TCP client disconnected: %1").arg(peer));
        updateNetworkStatus();
    });

    resetStatistics();
    setRunningState(false);
    updateNetworkStatus();
    statusBar()->showMessage(tr("Ready"));
    resize(1100, 720);
}

QWidget* MainWindow::buildControlPanel()
{
    // --- Kaynak ---
    m_sourceEdit = new QLineEdit;
    m_sourceEdit->setObjectName("sourceEdit");
    m_sourceEdit->setPlaceholderText(tr("0 = camera, or folder / video path"));
    m_browseFolderButton = new QPushButton(tr("Folder..."));
    m_browseVideoButton = new QPushButton(tr("Video..."));
    m_loopCheck = new QCheckBox(tr("Loop file sources"));
    m_loopCheck->setObjectName("loopCheck");
    m_loopCheck->setChecked(true);

    auto* browseRow = new QHBoxLayout;
    browseRow->addWidget(m_browseFolderButton);
    browseRow->addWidget(m_browseVideoButton);

    auto* sourceBox = new QGroupBox(tr("Source"));
    auto* sourceLayout = new QVBoxLayout(sourceBox);
    sourceLayout->addWidget(m_sourceEdit);
    sourceLayout->addLayout(browseRow);
    sourceLayout->addWidget(m_loopCheck);

    // --- Başlat / Durdur ---
    m_startButton = new QPushButton(tr("Start"));
    m_startButton->setObjectName("startButton");
    m_stopButton = new QPushButton(tr("Stop"));
    m_stopButton->setObjectName("stopButton");
    auto* runRow = new QHBoxLayout;
    runRow->addWidget(m_startButton);
    runRow->addWidget(m_stopButton);

    // --- Eşik: slider ve spin box birbirini takip eder ---
    m_thresholdSlider = new QSlider(Qt::Horizontal);
    m_thresholdSlider->setObjectName("thresholdSlider");
    m_thresholdSlider->setRange(0, 255);
    m_thresholdSpin = new QSpinBox;
    m_thresholdSpin->setObjectName("thresholdSpin");
    m_thresholdSpin->setRange(0, 255);
    m_thresholdSlider->setValue(m_pipeline.threshold());
    m_thresholdSpin->setValue(m_pipeline.threshold());

    auto* thresholdRow = new QHBoxLayout;
    thresholdRow->addWidget(m_thresholdSlider, 1);
    thresholdRow->addWidget(m_thresholdSpin);
    auto* thresholdBox = new QGroupBox(tr("Threshold (deviation from background)"));
    thresholdBox->setLayout(thresholdRow);

    // --- İstatistikler ---
    m_fpsLabel = new QLabel;
    m_fpsLabel->setObjectName("fpsLabel");
    m_latencyLabel = new QLabel;
    m_latencyLabel->setObjectName("latencyLabel");
    m_droppedLabel = new QLabel;
    m_droppedLabel->setObjectName("droppedLabel");
    m_defectsLabel = new QLabel;
    m_defectsLabel->setObjectName("defectsLabel");

    auto* statsBox = new QGroupBox(tr("Statistics"));
    auto* statsLayout = new QFormLayout(statsBox);
    statsLayout->addRow(tr("FPS:"), m_fpsLabel);
    statsLayout->addRow(tr("Latency:"), m_latencyLabel);
    statsLayout->addRow(tr("Dropped frames:"), m_droppedLabel);
    statsLayout->addRow(tr("Defective frames:"), m_defectsLabel);

    auto* panel = new QWidget;
    panel->setFixedWidth(300);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(sourceBox);
    layout->addLayout(runRow);
    layout->addWidget(thresholdBox);
    layout->addWidget(statsBox);
    layout->addWidget(buildNetworkBox());
    layout->addStretch(1);

    // --- Bağlantılar ---
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::start);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stop);
    connect(m_sourceEdit, &QLineEdit::returnPressed, this, &MainWindow::start);
    connect(m_browseFolderButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(m_browseVideoButton, &QPushButton::clicked, this, &MainWindow::browseVideo);

    // İki widget'ı senkron tutmak: setValue aynı değerle çağrılırsa sinyal yayınlanmaz,
    // bu yüzden karşılıklı bağlantı sonsuz döngüye girmez.
    connect(m_thresholdSlider, &QSlider::valueChanged, m_thresholdSpin, &QSpinBox::setValue);
    connect(m_thresholdSpin, qOverload<int>(&QSpinBox::valueChanged), m_thresholdSlider,
            &QSlider::setValue);
    connect(m_thresholdSlider, &QSlider::valueChanged, this, [this](int value) {
        m_pipeline.setThreshold(value);  // çalışırken de etkili (atomik)
        statusBar()->showMessage(tr("Threshold: %1").arg(value), 2000);
    });

    return panel;
}

QWidget* MainWindow::buildNetworkBox()
{
    m_tcpCheck = new QCheckBox(tr("TCP server, port"));
    m_tcpCheck->setObjectName("tcpCheck");
    m_tcpPortSpin = new QSpinBox;
    m_tcpPortSpin->setObjectName("tcpPortSpin");
    m_tcpPortSpin->setRange(1, 65535);
    m_tcpPortSpin->setValue(5555);

    m_udpCheck = new QCheckBox(tr("UDP to"));
    m_udpCheck->setObjectName("udpCheck");
    m_udpHostEdit = new QLineEdit("127.0.0.1");
    m_udpHostEdit->setObjectName("udpHostEdit");
    m_udpPortSpin = new QSpinBox;
    m_udpPortSpin->setObjectName("udpPortSpin");
    m_udpPortSpin->setRange(1, 65535);
    m_udpPortSpin->setValue(5556);

    m_networkLabel = new QLabel;
    m_networkLabel->setObjectName("networkLabel");
    m_networkLabel->setWordWrap(true);

    auto* box = new QGroupBox(tr("Network (JSON Lines)"));
    auto* grid = new QGridLayout(box);
    grid->addWidget(m_tcpCheck, 0, 0);
    grid->addWidget(m_tcpPortSpin, 0, 1, 1, 2);
    grid->addWidget(m_udpCheck, 1, 0);
    grid->addWidget(m_udpHostEdit, 1, 1);
    grid->addWidget(m_udpPortSpin, 1, 2);
    grid->addWidget(m_networkLabel, 2, 0, 1, 3);
    grid->setColumnStretch(1, 1);

    connect(m_tcpCheck, &QCheckBox::toggled, this, &MainWindow::onTcpToggled);
    connect(m_udpCheck, &QCheckBox::toggled, this, &MainWindow::onUdpToggled);
    return box;
}

void MainWindow::enableTcp(quint16 port)
{
    m_tcpPortSpin->setValue(port);
    m_tcpCheck->setChecked(true);  // toggled -> onTcpToggled
}

void MainWindow::enableUdp(const QString& host, quint16 port)
{
    m_udpHostEdit->setText(host);
    m_udpPortSpin->setValue(port);
    m_udpCheck->setChecked(true);
}

void MainWindow::onTcpToggled(bool enabled)
{
    if (enabled) {
        const auto port = static_cast<quint16>(m_tcpPortSpin->value());
        if (m_publisher.startTcpServer(port)) {
            appendLog(tr("TCP server listening on port %1").arg(port));
        } else {
            // Hata errorOccurred ile loglandı. Kutucuğu geri al ama toggled tekrar
            // tetiklenmesin diye sinyalleri geçici olarak engelle (RAII).
            const QSignalBlocker blocker(m_tcpCheck);
            m_tcpCheck->setChecked(false);
        }
    } else {
        m_publisher.stopTcpServer();
        appendLog(tr("TCP server stopped"));
    }
    m_tcpPortSpin->setEnabled(!m_tcpCheck->isChecked());
    updateNetworkStatus();
}

void MainWindow::onUdpToggled(bool enabled)
{
    if (enabled) {
        const QString host = m_udpHostEdit->text().trimmed();
        QHostAddress address;
        if (host.compare("localhost", Qt::CaseInsensitive) == 0) {
            address = QHostAddress::LocalHost;
        } else if (!address.setAddress(host)) {
            onError(tr("Invalid UDP host address: %1").arg(host));
            const QSignalBlocker blocker(m_udpCheck);
            m_udpCheck->setChecked(false);
            updateNetworkStatus();
            return;
        }
        const auto port = static_cast<quint16>(m_udpPortSpin->value());
        m_publisher.enableUdp(address, port);
        appendLog(tr("Sending UDP datagrams to %1:%2").arg(address.toString()).arg(port));
    } else {
        m_publisher.disableUdp();
        appendLog(tr("UDP sending stopped"));
    }
    const bool on = m_udpCheck->isChecked();
    m_udpHostEdit->setEnabled(!on);
    m_udpPortSpin->setEnabled(!on);
    updateNetworkStatus();
}

void MainWindow::updateNetworkStatus()
{
    const QString tcp = m_publisher.isTcpListening()
                            ? tr("TCP :%1, %n client(s)", nullptr, m_publisher.clientCount())
                                  .arg(m_publisher.tcpPort())
                            : tr("TCP off");
    const QString udp = m_publisher.isUdpEnabled()
                            ? tr("UDP to %1:%2").arg(m_udpHostEdit->text()).arg(m_udpPortSpin->value())
                            : tr("UDP off");
    m_networkLabel->setText(tcp + " | " + udp);
}

void MainWindow::setSource(const QString& source)
{
    m_sourceEdit->setText(source);
}

void MainWindow::setThreshold(int threshold)
{
    m_thresholdSlider->setValue(threshold);
}

void MainWindow::start()
{
    const QString source = m_sourceEdit->text().trimmed();
    if (source.isEmpty()) {
        onError(tr("Please choose a source first"));
        return;
    }
    resetStatistics();
    m_pipeline.start(source, m_loopCheck->isChecked());  // hata olursa onError çağrılır
}

void MainWindow::stop()
{
    m_pipeline.stop();
}

void MainWindow::browseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose image folder"),
                                                          m_sourceEdit->text());
    if (!dir.isEmpty()) {
        m_sourceEdit->setText(dir);
    }
}

void MainWindow::browseVideo()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Choose video file"), m_sourceEdit->text(),
        tr("Videos (*.mp4 *.avi *.mkv *.mov);;All files (*)"));
    if (!file.isEmpty()) {
        m_sourceEdit->setText(file);
    }
}

void MainWindow::onStarted(const QString& description)
{
    setRunningState(true);
    appendLog(tr("Started: %1").arg(description));
    statusBar()->showMessage(tr("Running: %1").arg(description));
}

void MainWindow::onStopped()
{
    setRunningState(false);
    m_fpsLabel->setText("0.0");
    appendLog(tr("Stopped after %1 frames (%2 defective)")
                  .arg(m_totalFrames)
                  .arg(m_defectiveFrames));
    statusBar()->showMessage(tr("Stopped"));
}

void MainWindow::onResult(const InspectionResult& result)
{
    // stop()'tan hemen önce kuyruğa girmiş bir sonuç durduktan sonra gelebilir; yok say.
    if (!m_pipeline.isRunning()) {
        return;
    }

    m_imageView->setImage(result.annotatedImage);

    ++m_totalFrames;
    m_fpsMeter.tick();
    m_fpsLabel->setText(QString::number(m_fpsMeter.rate(), 'f', 1));
    m_latencyLabel->setText(tr("%1 ms").arg(result.latencyMs, 0, 'f', 1));
    m_droppedLabel->setText(QString::number(result.droppedFrames));

    const auto& defects = result.detection.defects;
    if (!defects.empty()) {
        ++m_defectiveFrames;
        const Defect& largest = defects.front();  // alana göre sıralı
        appendLog(tr("Frame #%1: %n defect(s), largest %2 px² at (%3, %4)", nullptr,
                     static_cast<int>(defects.size()))
                      .arg(result.frameIndex)
                      .arg(largest.area, 0, 'f', 0)
                      .arg(static_cast<int>(largest.center.x))
                      .arg(static_cast<int>(largest.center.y)),
                  QColor(200, 60, 40));
    }
    m_defectsLabel->setText(tr("%1 / %2").arg(m_defectiveFrames).arg(m_totalFrames));
}

void MainWindow::onError(const QString& message)
{
    appendLog(tr("Error: %1").arg(message), Qt::red);
    statusBar()->showMessage(tr("Error: %1").arg(message), 5000);
}

void MainWindow::setRunningState(bool running)
{
    m_startButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_sourceEdit->setEnabled(!running);
    m_browseFolderButton->setEnabled(!running);
    m_browseVideoButton->setEnabled(!running);
    m_loopCheck->setEnabled(!running);
}

void MainWindow::resetStatistics()
{
    m_totalFrames = 0;
    m_defectiveFrames = 0;
    m_fpsMeter.reset();
    m_fpsLabel->setText("0.0");
    m_latencyLabel->setText("-");
    m_droppedLabel->setText("0");
    m_defectsLabel->setText("0 / 0");
}

void MainWindow::appendLog(const QString& text, const QColor& color)
{
    const QString time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString line = QStringLiteral("[%1] %2").arg(time, text.toHtmlEscaped());
    if (color.isValid()) {
        line = QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color.name(), line);
    }
    m_logView->appendHtml(line);
}

}  // namespace inspector
