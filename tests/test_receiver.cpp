// ReceiverClient testleri: gerçek ResultPublisher'a localhost üzerinden bağlanır.
// Yeniden bağlanma süreleri testlerde milisaniyelere indirilir.

#include "MessageFormat.h"
#include "ReceiverClient.h"
#include "inspector/ResultPublisher.h"

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
    result.latencyMs = 2.06;
    result.annotatedImage = QImage(640, 480, QImage::Format_RGB888);
    for (int i = 0; i < defectCount; ++i) {
        Defect d;
        d.boundingBox = cv::Rect(100 * i, 10, 20, 20);
        d.center = cv::Point2f(100.0f * static_cast<float>(i) + 10.0f, 20.0f);
        d.area = 100.0 * (i + 1);  // son kusur en büyük
        result.detection.defects.push_back(d);
    }
    return result;
}

/// İşletim sistemine boş bir port seçtirip bırakır.
quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return probe.serverPort();
}

}  // namespace

class ReceiverTest : public QObject {
    Q_OBJECT

private slots:
    void receivesMessagesFromPublisher();
    void reconnectsAfterServerRestart();
    void backsOffExponentiallyWhileServerIsDown();
    void stopCancelsReconnect();
    void invalidLinesAreCountedButNotFatal();
    void receivesUdpDatagrams();
    void formatMessageMatchesPythonClient();
};

void ReceiverTest::receivesMessagesFromPublisher()
{
    ResultPublisher publisher;
    QVERIFY(publisher.startTcpServer(0, QHostAddress::LocalHost));

    ReceiverClient client;
    QSignalSpy connected(&client, &ReceiverClient::connected);
    QSignalSpy messages(&client, &ReceiverClient::messageReceived);
    client.connectToServer("127.0.0.1", publisher.tcpPort());
    QVERIFY(connected.wait(3000));
    QTRY_COMPARE_WITH_TIMEOUT(publisher.clientCount(), 1, 3000);

    publisher.publish(makeResult(0, 0));
    publisher.publish(makeResult(1, 2));
    publisher.publish(makeResult(2, 1));
    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 3, 3000);

    const auto second = qvariant_cast<protocol::Message>(messages.at(1).at(0));
    QCOMPARE(second.frameIndex, quint64{1});
    QCOMPARE(second.defectCount, 2);
    QCOMPARE(client.stats().received, quint64{3});
    QCOMPARE(client.stats().defective, quint64{2});
    QCOMPARE(client.stats().invalid, quint64{0});
}

void ReceiverTest::reconnectsAfterServerRestart()
{
    const quint16 port = freePort();
    QVERIFY(port != 0);
    ResultPublisher publisher;
    QVERIFY(publisher.startTcpServer(port, QHostAddress::LocalHost));

    ReceiverClient client;
    client.setReconnectDelays(50, 400);
    QSignalSpy connected(&client, &ReceiverClient::connected);
    QSignalSpy disconnected(&client, &ReceiverClient::disconnected);
    QSignalSpy messages(&client, &ReceiverClient::messageReceived);
    client.connectToServer("127.0.0.1", port);
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 3000);

    publisher.stopTcpServer();  // sunucu "çöktü"
    QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 3000);

    QVERIFY(publisher.startTcpServer(port, QHostAddress::LocalHost));  // geri geldi
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 2, 5000);  // istemci kendiliğinden bağlandı
    QTRY_COMPARE_WITH_TIMEOUT(publisher.clientCount(), 1, 3000);

    publisher.publish(makeResult(42, 1));
    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 1, 3000);
    QCOMPARE(qvariant_cast<protocol::Message>(messages.at(0).at(0)).frameIndex, quint64{42});
}

void ReceiverTest::backsOffExponentiallyWhileServerIsDown()
{
    const quint16 port = freePort();  // dinleyen yok: her deneme "connection refused"
    ReceiverClient client;
    client.setReconnectDelays(20, 80);
    QSignalSpy scheduled(&client, &ReceiverClient::reconnectScheduled);
    client.connectToServer("127.0.0.1", port);

    QTRY_VERIFY_WITH_TIMEOUT(scheduled.count() >= 5, 3000);
    QList<int> delays;
    for (const auto& args : scheduled) {
        delays.append(args.at(0).toInt());
    }
    QCOMPARE(delays.mid(0, 5), (QList<int>{20, 40, 80, 80, 80}));  // ikiye katla, tavanda dur
    client.stop();
}

void ReceiverTest::stopCancelsReconnect()
{
    ReceiverClient client;
    client.setReconnectDelays(20, 20);
    QSignalSpy scheduled(&client, &ReceiverClient::reconnectScheduled);
    client.connectToServer("127.0.0.1", freePort());
    QTRY_VERIFY_WITH_TIMEOUT(scheduled.count() >= 1, 3000);

    client.stop();
    const auto countAtStop = scheduled.count();
    QTest::qWait(200);  // 10 deneme süresi kadar bekle
    QCOMPARE(scheduled.count(), countAtStop);
}

void ReceiverTest::invalidLinesAreCountedButNotFatal()
{
    // Protokole uymayan bir "sunucu": önce çöp, sonra geçerli bir mesaj gönderir.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QByteArray valid = protocol::encode(
        protocol::fromResult(makeResult(7, 1), QDateTime::currentDateTimeUtc()));
    connect(&server, &QTcpServer::newConnection, this, [&server, valid] {
        QTcpSocket* socket = server.nextPendingConnection();
        socket->write("this is not json\n");
        socket->write("{\"type\":\"inspection\",\"version\":99}\n");
        socket->write(valid);
    });

    ReceiverClient client;
    QSignalSpy invalid(&client, &ReceiverClient::invalidMessage);
    QSignalSpy messages(&client, &ReceiverClient::messageReceived);
    client.connectToServer("127.0.0.1", server.serverPort());

    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 1, 3000);  // bağlantı kopmadı, devam etti
    QCOMPARE(invalid.count(), 2);
    QCOMPARE(client.stats().invalid, quint64{2});
    QVERIFY(client.isConnected());
}

void ReceiverTest::receivesUdpDatagrams()
{
    ReceiverClient client;
    QVERIFY(client.listenUdp(0, QHostAddress::LocalHost));
    QSignalSpy messages(&client, &ReceiverClient::messageReceived);

    ResultPublisher publisher;
    publisher.enableUdp(QHostAddress::LocalHost, client.udpPort());
    publisher.publish(makeResult(5, 3));

    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 1, 3000);
    QCOMPARE(qvariant_cast<protocol::Message>(messages.at(0).at(0)).defectCount, 3);
}

void ReceiverTest::formatMessageMatchesPythonClient()
{
    const auto now = QDateTime::currentDateTimeUtc();
    // Aynı beklenen dizeler python/test_receiver.py içinde de kontrol edilir.
    QCOMPARE(formatMessage(protocol::fromResult(makeResult(18, 0), now)),
             QString("#18     OK  latency 2.1 ms"));
    QCOMPARE(formatMessage(protocol::fromResult(makeResult(17, 2), now)),
             QString("#17     2 defect(s)  largest 200 px² @ (110,20)  latency 2.1 ms"));
}

QTEST_GUILESS_MAIN(ReceiverTest)
#include "test_receiver.moc"
