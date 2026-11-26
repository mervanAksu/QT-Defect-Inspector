// MainWindow için Qt Test GUI testleri. Gerçek bir ekran gerekmez: ctest bu testi
// QT_QPA_PLATFORM=offscreen ile çalıştırır (CI sunucularında da çalışır).
// Widget'lara objectName ile ulaşılır; testler pencereyi kullanıcı gibi kullanır.

#include "ImageView.h"
#include "MainWindow.h"
#include "SampleGenerator.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
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
    QVERIFY(m_tmp.isValid());
    // Her ikisinde de kusur olan iki örnek görüntü.
    const auto a = samples::generateFabric({samples::DefectKind::Stain}, 1);
    const auto b = samples::generateFabric({samples::DefectKind::Hole, samples::DefectKind::Tear}, 2);
    QVERIFY(cv::imwrite(m_tmp.filePath("a.png").toStdString(), a.image));
    QVERIFY(cv::imwrite(m_tmp.filePath("b.png").toStdString(), b.image));
}

void MainWindowTest::init()
{
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

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"
