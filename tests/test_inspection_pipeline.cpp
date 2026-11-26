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

QTEST_GUILESS_MAIN(InspectionPipelineTest)
#include "test_inspection_pipeline.moc"
