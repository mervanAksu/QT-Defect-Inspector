// Ağ katmanı testleri (Qt Test): protokol, satır çerçeveleme ve localhost üzerinde
// gerçek TCP/UDP soketleriyle ResultPublisher.

#include "inspector/LineBuffer.h"
#include "inspector/Protocol.h"
#include "inspector/ResultPublisher.h"

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QtTest>

using namespace inspector;

namespace {

InspectionResult makeResult(quint64 frame, int defectCount)
{
    InspectionResult result;
    result.frameIndex = frame;
    result.latencyMs = 3.5;
    result.droppedFrames = 2;
    result.detection.processingMs = 1.25;
    result.annotatedImage = QImage(640, 480, QImage::Format_RGB888);
    for (int i = 0; i < defectCount; ++i) {
        Defect d;
        d.boundingBox = cv::Rect(10 * i, 20, 8, 9);
        d.center = cv::Point2f(10.0f * static_cast<float>(i) + 4.0f, 24.5f);
        d.area = 60.0 + i;
        result.detection.defects.push_back(d);
    }
    return result;
}

/// Soketten gelenleri okur, satırlara böler, çözer. Toplam mesaj sayısını döner.
int drain(QTcpSocket& socket, LineBuffer& buffer, QList<protocol::Message>& out)
{
    buffer.append(socket.readAll());
    while (const auto line = buffer.nextLine()) {
        if (auto message = protocol::decode(*line)) {
            out.append(*message);
        }
    }
    return static_cast<int>(out.size());
}

}  // namespace

class NetworkTest : public QObject {
    Q_OBJECT

private slots:
    // Protokol
    void encodeProducesOneTerminatedLine();
    void encodeDecodeRoundTrip();
    void listsAtMost50DefectsButKeepsTotal();
    void decodeRejectsInvalidMessages_data();
    void decodeRejectsInvalidMessages();

    // Çerçeveleme
    void lineBufferSplitsMultipleMessagesInOneChunk();
    void lineBufferReassemblesByteByByte();
    void lineBufferStripsCarriageReturn();
    void lineBufferDiscardsOverlongLines();

    // Soketler
    void tcpClientsReceiveAllMessagesInOrder();
    void tcpDisconnectIsTracked();
    void udpTargetReceivesOneMessagePerDatagram();
    void startFailsWhenPortIsBusy();
    void nothingIsEncodedWithoutListeners();
};

// --- Protokol ------------------------------------------------------------------

void NetworkTest::encodeProducesOneTerminatedLine()
{
    const QByteArray bytes =
        protocol::encode(protocol::fromResult(makeResult(7, 3), QDateTime::currentDateTimeUtc()));
    QVERIFY(bytes.endsWith('\n'));
    QCOMPARE(bytes.count('\n'), 1);  // mesajın içinde yeni satır yok
}

void NetworkTest::encodeDecodeRoundTrip()
{
    const QDateTime sentAt = QDateTime::fromString("2026-11-26T14:44:56.759Z", Qt::ISODateWithMs);
    const protocol::Message original = protocol::fromResult(makeResult(17, 2), sentAt);

    QString error;
    const auto decoded = protocol::decode(protocol::encode(original), &error);
    QVERIFY2(decoded.has_value(), qPrintable(error));

    QCOMPARE(decoded->frameIndex, quint64{17});
    QCOMPARE(decoded->sentAt, sentAt);
    QCOMPARE(decoded->defectCount, 2);
    QCOMPARE(decoded->processingMs, 1.25);
    QCOMPARE(decoded->latencyMs, 3.5);
    QCOMPARE(decoded->droppedFrames, quint64{2});
    QCOMPARE(decoded->imageSize, QSize(640, 480));
    QCOMPARE(decoded->defects.size(), 2);
    QCOMPARE(decoded->defects[1].box, QRect(10, 20, 8, 9));
    QCOMPARE(decoded->defects[1].center, QPointF(14.0, 24.5));
    QCOMPARE(decoded->defects[1].area, 61.0);
}

void NetworkTest::listsAtMost50DefectsButKeepsTotal()
{
    const auto message = protocol::fromResult(makeResult(1, 60), QDateTime::currentDateTimeUtc());
    QCOMPARE(message.defectCount, 60);
    QCOMPARE(message.defects.size(), protocol::kMaxListedDefects);
    QVERIFY(protocol::decode(protocol::encode(message)).has_value());
}

void NetworkTest::decodeRejectsInvalidMessages_data()
{
    QTest::addColumn<QByteArray>("line");
    const QByteArray base =
        R"("frame":1,"defect_count":0,"processing_ms":1,"latency_ms":1,"dropped_frames":0,"defects":[])";

    QTest::newRow("not json") << QByteArray("hello");
    QTest::newRow("array") << QByteArray("[1,2,3]");
    QTest::newRow("wrong type") << QByteArray(R"({"type":"other","version":1,)" + base + "}");
    QTest::newRow("future version") << QByteArray(R"({"type":"inspection","version":2,)" + base + "}");
    QTest::newRow("missing frame")
        << QByteArray(R"({"type":"inspection","version":1,"defect_count":0,"processing_ms":1,)"
                      R"("latency_ms":1,"dropped_frames":0})");
    QTest::newRow("frame is text")
        << QByteArray(R"({"type":"inspection","version":1,"frame":"1","defect_count":0,)"
                      R"("processing_ms":1,"latency_ms":1,"dropped_frames":0})");
    QTest::newRow("more defects than count")
        << QByteArray(R"({"type":"inspection","version":1,"frame":1,"defect_count":0,)"
                      R"("processing_ms":1,"latency_ms":1,"dropped_frames":0,)"
                      R"("defects":[{"x":1,"y":1,"width":1,"height":1,"cx":1,"cy":1,"area":1}]})");
}

void NetworkTest::decodeRejectsInvalidMessages()
{
    QFETCH(QByteArray, line);
    QString error;
    QVERIFY(!protocol::decode(line, &error).has_value());
    QVERIFY(!error.isEmpty());
}

// --- Çerçeveleme ---------------------------------------------------------------

void NetworkTest::lineBufferSplitsMultipleMessagesInOneChunk()
{
    LineBuffer buffer;
    buffer.append("one\ntwo\nthr");

    QCOMPARE(buffer.nextLine(), std::optional<QByteArray>("one"));
    QCOMPARE(buffer.nextLine(), std::optional<QByteArray>("two"));
    QVERIFY(!buffer.nextLine().has_value());  // "thr" henüz tamamlanmadı
    QCOMPARE(buffer.pendingBytes(), qsizetype{3});

    buffer.append("ee\n");
    QCOMPARE(buffer.nextLine(), std::optional<QByteArray>("three"));
}

void NetworkTest::lineBufferReassemblesByteByByte()
{
    const QByteArray message =
        protocol::encode(protocol::fromResult(makeResult(5, 1), QDateTime::currentDateTimeUtc()));
    LineBuffer buffer;
    for (int i = 0; i < message.size() - 1; ++i) {
        buffer.append(message.mid(i, 1));
        QVERIFY(!buffer.nextLine().has_value());
    }
    buffer.append(message.right(1));  // son bayt: '\n'
    const auto line = buffer.nextLine();
    QVERIFY(line.has_value());
    QCOMPARE(protocol::decode(*line)->frameIndex, quint64{5});
}

void NetworkTest::lineBufferStripsCarriageReturn()
{
    LineBuffer buffer;
    buffer.append("windows\r\n");
    QCOMPARE(buffer.nextLine(), std::optional<QByteArray>("windows"));
}

void NetworkTest::lineBufferDiscardsOverlongLines()
{
    LineBuffer buffer(10);

    buffer.append("aaaaaaaaaaaaaaaaaaaa");  // 20 bayt, '\n' yok -> hemen atılır
    QCOMPARE(buffer.pendingBytes(), qsizetype{0});
    QCOMPARE(buffer.discardedLines(), qsizetype{1});

    buffer.append("aaa\nok\n");  // ilk parça uzun satırın kuyruğu
    QCOMPARE(buffer.nextLine(), std::optional<QByteArray>("ok"));

    buffer.append("0123456789AB\nfine\n");  // tek parçada gelen uzun satır
    QCOMPARE(buffer.nextLine(), std::optional<QByteArray>("fine"));
    QCOMPARE(buffer.discardedLines(), qsizetype{2});
}

// --- Soketler --------------------------------------------------------------------

void NetworkTest::tcpClientsReceiveAllMessagesInOrder()
{
    ResultPublisher publisher;
    QVERIFY(publisher.startTcpServer(0, QHostAddress::LocalHost));  // 0 = boş port seç

    QTcpSocket a;
    QTcpSocket b;
    a.connectToHost(QHostAddress::LocalHost, publisher.tcpPort());
    b.connectToHost(QHostAddress::LocalHost, publisher.tcpPort());
    QTRY_COMPARE_WITH_TIMEOUT(publisher.clientCount(), 2, 3000);

    for (quint64 frame = 0; frame < 5; ++frame) {
        publisher.publish(makeResult(frame, static_cast<int>(frame % 3)));
    }

    // QTRY_* arada olay döngüsünü çalıştırır; sunucu soketleri ancak böyle veri gönderir.
    LineBuffer bufA;
    LineBuffer bufB;
    QList<protocol::Message> gotA;
    QList<protocol::Message> gotB;
    QTRY_COMPARE_WITH_TIMEOUT(drain(a, bufA, gotA), 5, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(drain(b, bufB, gotB), 5, 3000);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(gotA[i].frameIndex, quint64(i));  // TCP sırayı korur
        QCOMPARE(gotB[i].defectCount, i % 3);
    }
    QCOMPARE(publisher.messagesPublished(), quint64{5});
}

void NetworkTest::tcpDisconnectIsTracked()
{
    ResultPublisher publisher;
    QVERIFY(publisher.startTcpServer(0, QHostAddress::LocalHost));
    QSignalSpy connected(&publisher, &ResultPublisher::clientConnected);
    QSignalSpy disconnected(&publisher, &ResultPublisher::clientDisconnected);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, publisher.tcpPort());
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 3000);

    client.disconnectFromHost();
    QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 3000);
    QCOMPARE(publisher.clientCount(), 0);

    publisher.publish(makeResult(1, 1));  // bağlantısı kopmuş istemciye yazmaya çalışmamalı
}

void NetworkTest::udpTargetReceivesOneMessagePerDatagram()
{
    QUdpSocket receiver;
    QVERIFY(receiver.bind(QHostAddress::LocalHost, 0));

    ResultPublisher publisher;
    publisher.enableUdp(QHostAddress::LocalHost, receiver.localPort());
    publisher.publish(makeResult(3, 2));
    publisher.publish(makeResult(4, 0));

    QList<protocol::Message> got;
    auto receive = [&] {
        while (receiver.hasPendingDatagrams()) {
            if (auto m = protocol::decode(receiver.receiveDatagram().data())) {
                got.append(*m);
            }
        }
        return static_cast<int>(got.size());
    };
    QTRY_COMPARE_WITH_TIMEOUT(receive(), 2, 3000);
    QCOMPARE(got[0].frameIndex, quint64{3});
    QCOMPARE(got[0].defects.size(), 2);
}

void NetworkTest::startFailsWhenPortIsBusy()
{
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));

    ResultPublisher publisher;
    QSignalSpy errors(&publisher, &ResultPublisher::errorOccurred);
    QVERIFY(!publisher.startTcpServer(blocker.serverPort(), QHostAddress::LocalHost));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!publisher.isTcpListening());
}

void NetworkTest::nothingIsEncodedWithoutListeners()
{
    ResultPublisher publisher;
    publisher.publish(makeResult(1, 1));
    QCOMPARE(publisher.messagesPublished(), quint64{0});
}

QTEST_GUILESS_MAIN(NetworkTest)
#include "test_network.moc"
