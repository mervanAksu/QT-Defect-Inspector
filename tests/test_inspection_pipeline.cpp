// Qt Test ile yazılmış entegrasyon testleri: gerçek thread'ler, gerçek kuyruk,
// gerçek dedektör. GoogleTest birim testlerini tamamlar; sinyal/slot ve olay
// döngüsü gerektiren senaryoları QSignalSpy ve QTRY_* makrolarıyla test eder.

#include "inspector/InspectionPipeline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using inspector::InspectionPipeline;
using inspector::InspectionResult;

namespace {

/// 320x240 gri kumaş; her (merkez, gri) çifti için bir leke çizer.
void writeFabric(const QString& path, std::initializer_list<std::pair<cv::Point, int>> spots)
{
    cv::Mat image(240, 320, CV_8UC3, cv::Scalar(180, 180, 180));
    for (const auto& [center, gray] : spots) {
        cv::circle(image, center, 12, cv::Scalar(gray, gray, gray), cv::FILLED);
    }
    QVERIFY(cv::imwrite(path.toStdString(), image));
}

InspectionResult resultAt(const QSignalSpy& spy, int i)
{
    return qvariant_cast<InspectionResult>(spy.at(i).at(0));
}

/// Gerçek kamera olmadan "kablo çıktı / geri takıldı" senaryosu: `framesBeforeLoss`
/// kare verdikten sonra kopar; ilk `failedReopens` yeniden açma denemesi başarısız,
/// sonrakinde geri gelir ve kare vermeye devam eder.
/// Not: readImage()/reopen() capture thread'inde çağrılır; test bu nesneye pipeline'a
/// verdikten sonra dokunmaz, yalnızca sinyalleri gözler (paylaşılan durum yok).
class FakeCamera final : public inspector::FrameSource {
public:
    FakeCamera(int framesBeforeLoss, int failedReopens)
        : m_framesBeforeLoss(framesBeforeLoss), m_failedReopens(failedReopens)
    {
    }

    std::string description() const override { return "fake-camera"; }
    bool isLive() const noexcept override { return true; }
    double fps() const noexcept override { return 0.0; }
    bool canReopen() const noexcept override { return true; }

    bool reopen() override
    {
        if (++m_reopenCalls <= m_failedReopens) {
            return false;
        }
        m_connected = true;
        return true;
    }

protected:
    bool readImage(cv::Mat& out) override
    {
        if (!m_connected) {
            return false;
        }
        if (!m_lostOnce && m_produced == m_framesBeforeLoss) {
            m_lostOnce = true;
            m_connected = false;  // kablo çıktı
            return false;
        }
        ++m_produced;
        out = cv::Mat(48, 64, CV_8UC3, cv::Scalar(180, 180, 180));
        QThread::msleep(10);  // kamera ~100 FPS'te kare verir
        return true;
    }

private:
    const int m_framesBeforeLoss;
    const int m_failedReopens;
    int m_produced = 0;
    int m_reopenCalls = 0;
    bool m_connected = true;
    bool m_lostOnce = false;
};

}  // namespace

class InspectionPipelineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void startFailsForMissingSource();
    void processesFolderInOrderAndStopsAtEnd();
    void resultsArriveOnTheOwnerThread();
    void stopIsFastIdempotentAndRestartable();
    void startWhileRunningIsRejected();
    void thresholdChangeAppliesWhileRunning();
    void destructorStopsRunningPipeline();
    void recoversWhenLiveSourceComesBack();
    void stopWhileReconnectingIsFast();

private:
    QTemporaryDir m_tmp;
    QString m_mixedDir;  // temiz, 1 kusurlu, 2 kusurlu
    QString m_faintDir;  // yalnızca hafif kontrastlı bir leke
};

void InspectionPipelineTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    QDir root(m_tmp.path());
    QVERIFY(root.mkdir("mixed"));
    QVERIFY(root.mkdir("faint"));
    m_mixedDir = root.filePath("mixed");
    m_faintDir = root.filePath("faint");

    writeFabric(m_mixedDir + "/a_clean.png", {});
    writeFabric(m_mixedDir + "/b_one.png", {{{100, 100}, 30}});
    writeFabric(m_mixedDir + "/c_two.png", {{{80, 80}, 30}, {{240, 160}, 250}});
    writeFabric(m_faintDir + "/faint.png", {{{160, 120}, 130}});  // zeminden 50 koyu
}

void InspectionPipelineTest::startFailsForMissingSource()
{
    InspectionPipeline pipeline;
    QSignalSpy errors(&pipeline, &InspectionPipeline::errorOccurred);

    QVERIFY(!pipeline.start(m_tmp.filePath("missing.mp4")));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!pipeline.isRunning());
}

void InspectionPipelineTest::processesFolderInOrderAndStopsAtEnd()
{
    InspectionPipeline pipeline;
    QSignalSpy results(&pipeline, &InspectionPipeline::resultReady);
    QSignalSpy stopped(&pipeline, &InspectionPipeline::stopped);

    QVERIFY(pipeline.start(m_mixedDir, /*loop=*/false));
    QVERIFY(stopped.wait(5000));  // kaynak bitince kendiliğinden durmalı
    QVERIFY(!pipeline.isRunning());

    QCOMPARE(results.count(), 3);
    const std::size_t expectedDefects[] = {0, 1, 2};
    for (int i = 0; i < 3; ++i) {
        const InspectionResult r = resultAt(results, i);
        QCOMPARE(r.frameIndex, static_cast<std::uint64_t>(i));
        QCOMPARE(r.detection.defects.size(), expectedDefects[i]);
        QCOMPARE(r.annotatedImage.size(), QSize(320, 240));
        QCOMPARE(r.droppedFrames, std::size_t{0});
    }
}

void InspectionPipelineTest::resultsArriveOnTheOwnerThread()
{
    InspectionPipeline pipeline;
    QThread* receivedOn = nullptr;
    connect(&pipeline, &InspectionPipeline::resultReady, this,
            [&receivedOn](const InspectionResult&) { receivedOn = QThread::currentThread(); });

    QVERIFY(pipeline.start(m_mixedDir));
    QTRY_VERIFY_WITH_TIMEOUT(receivedOn != nullptr, 3000);
    pipeline.stop();

    // Slot, worker thread'inde değil, pipeline'ın yaşadığı (ana) thread'de çalışmalı.
    QCOMPARE(receivedOn, QCoreApplication::instance()->thread());
}

void InspectionPipelineTest::stopIsFastIdempotentAndRestartable()
{
    InspectionPipeline pipeline;
    QSignalSpy results(&pipeline, &InspectionPipeline::resultReady);
    QSignalSpy stopped(&pipeline, &InspectionPipeline::stopped);

    QVERIFY(pipeline.start(m_mixedDir));
    QVERIFY(results.wait(3000));

    QElapsedTimer timer;
    timer.start();
    pipeline.stop();
    QVERIFY2(timer.elapsed() < 1000, "stop() took too long");
    QVERIFY(!pipeline.isRunning());
    QCOMPARE(stopped.count(), 1);

    pipeline.stop();  // ikinci çağrı etkisiz olmalı
    QCOMPARE(stopped.count(), 1);

    results.clear();
    QVERIFY(pipeline.start(m_mixedDir));  // yeniden başlatılabilmeli
    QVERIFY(results.wait(3000));
    pipeline.stop();
}

void InspectionPipelineTest::startWhileRunningIsRejected()
{
    InspectionPipeline pipeline;
    QSignalSpy errors(&pipeline, &InspectionPipeline::errorOccurred);

    QVERIFY(pipeline.start(m_mixedDir));
    QVERIFY(!pipeline.start(m_mixedDir));
    QCOMPARE(errors.count(), 1);
    QVERIFY(pipeline.isRunning());
    pipeline.stop();
}

void InspectionPipelineTest::thresholdChangeAppliesWhileRunning()
{
    InspectionPipeline pipeline;
    pipeline.setThreshold(80);  // hafif leke (kontrast ~50) bu eşikte görünmez

    bool sawCleanFrame = false;
    bool sawDefect = false;
    connect(&pipeline, &InspectionPipeline::resultReady, this,
            [&](const InspectionResult& r) {
                (r.detection.hasDefects() ? sawDefect : sawCleanFrame) = true;
            });

    QVERIFY(pipeline.start(m_faintDir));
    QTRY_VERIFY_WITH_TIMEOUT(sawCleanFrame, 3000);
    QVERIFY(!sawDefect);

    pipeline.setThreshold(30);  // çalışırken hassasiyeti artır
    QCOMPARE(pipeline.threshold(), 30);
    QTRY_VERIFY_WITH_TIMEOUT(sawDefect, 3000);
    pipeline.stop();
}

void InspectionPipelineTest::destructorStopsRunningPipeline()
{
    QElapsedTimer timer;
    timer.start();
    {
        InspectionPipeline pipeline;
        QSignalSpy results(&pipeline, &InspectionPipeline::resultReady);
        QVERIFY(pipeline.start(m_mixedDir));
        QVERIFY(results.wait(3000));
    }  // burada thread'ler hâlâ çalışıyor; destructor temiz kapatmalı (çökme/asılma yok)
    QVERIFY(timer.elapsed() < 4000);
}

void InspectionPipelineTest::recoversWhenLiveSourceComesBack()
{
    InspectionPipeline pipeline;
    pipeline.setReconnectPolicy({10, 40});  // testte milisaniyeler yeter
    QSignalSpy lost(&pipeline, &InspectionPipeline::sourceLost);
    QSignalSpy recovered(&pipeline, &InspectionPipeline::sourceRecovered);
    QSignalSpy results(&pipeline, &InspectionPipeline::resultReady);
    QSignalSpy stopped(&pipeline, &InspectionPipeline::stopped);

    QVERIFY(pipeline.start(std::make_unique<FakeCamera>(5, 2)));

    QTRY_COMPARE_WITH_TIMEOUT(recovered.count(), 1, 3000);
    QCOMPARE(lost.count(), 1);
    QCOMPARE(lost.at(0).at(0).toString(), QString("fake-camera"));
    QCOMPARE(recovered.at(0).at(0).toInt(), 3);  // 2 başarısız deneme + 1 başarılı

    // Akış kopmadan devam etmeli: kopmadan önceki 5 kareden fazlası gelmeli.
    QTRY_VERIFY_WITH_TIMEOUT(results.count() >= 15, 3000);
    QVERIFY(pipeline.isRunning());
    QCOMPARE(stopped.count(), 0);  // kopma pipeline'ı durdurmadı
    for (int i = 1; i < results.count(); ++i) {
        QVERIFY(resultAt(results, i).frameIndex > resultAt(results, i - 1).frameIndex);
    }
    pipeline.stop();
}

void InspectionPipelineTest::stopWhileReconnectingIsFast()
{
    InspectionPipeline pipeline;
    pipeline.setReconnectPolicy({50, 2000});
    QSignalSpy lost(&pipeline, &InspectionPipeline::sourceLost);

    QVERIFY(pipeline.start(std::make_unique<FakeCamera>(3, 1'000'000)));  // hiç geri gelmez
    QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
    QTest::qWait(400);  // bekleme süresi 2 sn'lik tavana tırmanırken

    QElapsedTimer timer;
    timer.start();
    pipeline.stop();
    QVERIFY2(timer.elapsed() < 500, "stop() waited for the reconnect delay");
    QVERIFY(!pipeline.isRunning());
}

QTEST_GUILESS_MAIN(InspectionPipelineTest)
#include "test_inspection_pipeline.moc"
