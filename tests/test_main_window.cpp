// MainWindow için Qt Test GUI testleri. Gerçek bir ekran gerekmez: ctest bu testi
// QT_QPA_PLATFORM=offscreen ile çalıştırır (CI sunucularında da çalışır).
// Widget'lara objectName ile ulaşılır; testler pencereyi kullanıcı gibi kullanır.

#include "ImageView.h"
#include "MainWindow.h"
#include "SampleGenerator.h"
#include "inspector/LineBuffer.h"
#include "inspector/Protocol.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>

using inspector::ImageView;
using inspector::MainWindow;
namespace samples = inspector::samples;

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void startsInIdleState();
    void runShowsImagesStatisticsAndDefectLog();
    void stopButtonReturnsToIdle();
    void finiteSourceReturnsToIdleByItself();
    void invalidSourceIsLoggedAndStaysIdle();
    void sliderAndSpinBoxStayInSync();
    void tcpCheckboxPublishesLiveResults();
    void invalidUdpHostIsRejected();
    void settingsAreRestoredAfterRestart();
    void savedTcpServerStartsOnLaunch();

private:
    template <typename T>
    T* child(const char* name) const
    {
        auto* widget = m_window->findChild<T*>(name);
        if (!widget) {
            qFatal("widget not found: %s", name);
        }
        return widget;
    }

    QTemporaryDir m_tmp;
    MainWindow* m_window = nullptr;
};

void MainWindowTest::initTestCase()
{
    // QSettings testlerde kullanıcının gerçek ~/.config'ine dokunmasın: test modunda
    // Qt ayrı bir klasör kullanır (~/.qttest/...).
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("DefectInspectorTests");
    QCoreApplication::setApplicationName("ui_tests");

    QVERIFY(m_tmp.isValid());
    // Her ikisinde de kusur olan iki örnek görüntü.
    const auto a = samples::generateFabric({samples::DefectKind::Stain}, 1);
    const auto b = samples::generateFabric({samples::DefectKind::Hole, samples::DefectKind::Tear}, 2);
    QVERIFY(cv::imwrite(m_tmp.filePath("a.png").toStdString(), a.image));
    QVERIFY(cv::imwrite(m_tmp.filePath("b.png").toStdString(), b.image));
}

void MainWindowTest::init()
{
    QSettings().clear();  // her test temiz ayarlarla başlasın
    m_window = new MainWindow;
    m_window->show();
}

void MainWindowTest::cleanup()
{
    delete m_window;  // çalışan bir pipeline varsa destructor durdurur
    m_window = nullptr;
}

void MainWindowTest::startsInIdleState()
{
    QVERIFY(child<QPushButton>("startButton")->isEnabled());
    QVERIFY(!child<QPushButton>("stopButton")->isEnabled());
    QVERIFY(child<ImageView>("imageView")->image().isNull());
}

void MainWindowTest::runShowsImagesStatisticsAndDefectLog()
{
    child<QLineEdit>("sourceEdit")->setText(m_tmp.path());
    QTest::mouseClick(child<QPushButton>("startButton"), Qt::LeftButton);

    QVERIFY(!child<QPushButton>("startButton")->isEnabled());
    QVERIFY(child<QPushButton>("stopButton")->isEnabled());
    QVERIFY(!child<QLineEdit>("sourceEdit")->isEnabled());

    auto* view = child<ImageView>("imageView");
    QTRY_VERIFY_WITH_TIMEOUT(!view->image().isNull(), 3000);
    QCOMPARE(view->image().size(), QSize(640, 480));

    auto* log = child<QPlainTextEdit>("logView");
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains("defect"), 3000);
    QVERIFY(log->toPlainText().contains("Started"));

    // İki karenin ikisi de kusurlu: "Defective frames" sayacı ilerlemeli.
    QTRY_VERIFY_WITH_TIMEOUT(child<QLabel>("defectsLabel")->text().startsWith("2 /"), 3000);
}

void MainWindowTest::stopButtonReturnsToIdle()
{
    child<QLineEdit>("sourceEdit")->setText(m_tmp.path());
    QTest::mouseClick(child<QPushButton>("startButton"), Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!child<ImageView>("imageView")->image().isNull(), 3000);

    QTest::mouseClick(child<QPushButton>("stopButton"), Qt::LeftButton);

    QVERIFY(child<QPushButton>("startButton")->isEnabled());
    QVERIFY(!child<QPushButton>("stopButton")->isEnabled());
    QVERIFY(child<QLineEdit>("sourceEdit")->isEnabled());
    QVERIFY(child<QPlainTextEdit>("logView")->toPlainText().contains("Stopped"));
}

void MainWindowTest::finiteSourceReturnsToIdleByItself()
{
    child<QCheckBox>("loopCheck")->setChecked(false);
    child<QLineEdit>("sourceEdit")->setText(m_tmp.path());
    QTest::mouseClick(child<QPushButton>("startButton"), Qt::LeftButton);

    // 2 görüntü bitince pipeline kendiliğinden durur, butonlar boşta durumuna döner.
    QTRY_VERIFY_WITH_TIMEOUT(child<QPushButton>("startButton")->isEnabled(), 5000);
    QCOMPARE(child<QLabel>("defectsLabel")->text(), QString("2 / 2"));
}

void MainWindowTest::invalidSourceIsLoggedAndStaysIdle()
{
    child<QLineEdit>("sourceEdit")->setText(m_tmp.filePath("missing.mp4"));
    QTest::mouseClick(child<QPushButton>("startButton"), Qt::LeftButton);

    QVERIFY(child<QPushButton>("startButton")->isEnabled());
    QVERIFY(!child<QPushButton>("stopButton")->isEnabled());
    QVERIFY(child<QPlainTextEdit>("logView")->toPlainText().contains("Error"));
}

void MainWindowTest::sliderAndSpinBoxStayInSync()
{
    auto* slider = child<QSlider>("thresholdSlider");
    auto* spin = child<QSpinBox>("thresholdSpin");

    slider->setValue(42);
    QCOMPARE(spin->value(), 42);

    spin->setValue(120);
    QCOMPARE(slider->value(), 120);
}

void MainWindowTest::tcpCheckboxPublishesLiveResults()
{
    // Boş bir port bul: işletim sistemine seçtir, sonra bırak.
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    child<QSpinBox>("tcpPortSpin")->setValue(port);
    child<QCheckBox>("tcpCheck")->setChecked(true);
    QVERIFY(child<QLabel>("networkLabel")->text().contains(QString::number(port)));
    QVERIFY(!child<QSpinBox>("tcpPortSpin")->isEnabled());

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, port);
    QTRY_VERIFY_WITH_TIMEOUT(child<QLabel>("networkLabel")->text().contains("1 client"), 3000);

    child<QLineEdit>("sourceEdit")->setText(m_tmp.path());
    QTest::mouseClick(child<QPushButton>("startButton"), Qt::LeftButton);

    // Ekrandaki sonuçla aynı veri ağdan da gelmeli: iki örnekte de kusur var.
    inspector::LineBuffer buffer;
    std::optional<inspector::protocol::Message> first;
    auto receive = [&] {
        buffer.append(client.readAll());
        if (const auto line = buffer.nextLine()) {
            first = inspector::protocol::decode(*line);
        }
        return first.has_value();
    };
    QTRY_VERIFY_WITH_TIMEOUT(receive(), 3000);
    QCOMPARE(first->frameIndex, quint64{0});
    QVERIFY(first->defectCount > 0);
    QCOMPARE(first->imageSize, QSize(640, 480));
}

void MainWindowTest::invalidUdpHostIsRejected()
{
    child<QLineEdit>("udpHostEdit")->setText("not-an-ip");
    child<QCheckBox>("udpCheck")->setChecked(true);

    QVERIFY(!child<QCheckBox>("udpCheck")->isChecked());  // geri alındı
    QVERIFY(child<QLineEdit>("udpHostEdit")->isEnabled());
    QVERIFY(child<QPlainTextEdit>("logView")->toPlainText().contains("Invalid UDP host"));
}

void MainWindowTest::settingsAreRestoredAfterRestart()
{
    child<QLineEdit>("sourceEdit")->setText("/data/line3/camera-dump");
    child<QCheckBox>("loopCheck")->setChecked(false);
    child<QSlider>("thresholdSlider")->setValue(77);
    child<QSpinBox>("tcpPortSpin")->setValue(6001);
    child<QLineEdit>("udpHostEdit")->setText("10.0.0.5");
    child<QSpinBox>("udpPortSpin")->setValue(6002);

    m_window->close();  // closeEvent -> ayarlar kaydedilir
    delete m_window;
    m_window = new MainWindow;  // "uygulamayı yeniden başlat"

    QCOMPARE(child<QLineEdit>("sourceEdit")->text(), QString("/data/line3/camera-dump"));
    QVERIFY(!child<QCheckBox>("loopCheck")->isChecked());
    QCOMPARE(child<QSlider>("thresholdSlider")->value(), 77);
    QCOMPARE(child<QSpinBox>("thresholdSpin")->value(), 77);
    QCOMPARE(child<QSpinBox>("tcpPortSpin")->value(), 6001);
    QCOMPARE(child<QLineEdit>("udpHostEdit")->text(), QString("10.0.0.5"));
    QCOMPARE(child<QSpinBox>("udpPortSpin")->value(), 6002);
    QVERIFY(!child<QCheckBox>("tcpCheck")->isChecked());
}

void MainWindowTest::savedTcpServerStartsOnLaunch()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    delete m_window;
    {
        QSettings settings;
        settings.setValue("network/tcpEnabled", true);
        settings.setValue("network/tcpPort", port);
    }
    m_window = new MainWindow;

    QVERIFY(child<QCheckBox>("tcpCheck")->isChecked());
    QVERIFY(child<QLabel>("networkLabel")->text().contains(QString::number(port)));
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(client.waitForConnected(3000));  // sunucu gerçekten dinliyor
}

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"
