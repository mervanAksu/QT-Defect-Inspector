#include "inspector/ResultPublisher.h"

#include "inspector/Protocol.h"

#include <QDateTime>
#include <QTcpSocket>

#include <utility>

namespace inspector {

namespace {

QString peerName(const QTcpSocket* socket)
{
    // Sunucu IPv4+IPv6 (dual-stack) dinlediği için IPv4 istemciler "::ffff:127.0.0.1"
    // gibi IPv4-mapped IPv6 adresiyle görünür; okunaklı olsun diye düz IPv4'e çeviriyoruz.
    QHostAddress address = socket->peerAddress();
    bool isIPv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIPv4);
    if (isIPv4) {
        address = QHostAddress(ipv4);
    }
    return QStringLiteral("%1:%2").arg(address.toString()).arg(socket->peerPort());
}

}  // namespace

ResultPublisher::ResultPublisher(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<InspectionResult>();
    connect(&m_server, &QTcpServer::newConnection, this, &ResultPublisher::onNewConnection);
}

ResultPublisher::~ResultPublisher()
{
    stopTcpServer();
}

bool ResultPublisher::startTcpServer(quint16 port, const QHostAddress& address)
{
    stopTcpServer();
    if (!m_server.listen(address, port)) {
        emit errorOccurred(tr("Cannot listen on TCP port %1: %2")
                               .arg(port)
                               .arg(m_server.errorString()));
        return false;
    }
    return true;
}

void ResultPublisher::stopTcpServer()
{
    m_server.close();  // yeni bağlantı kabul etme
    for (QTcpSocket* socket : std::as_const(m_clients)) {
        socket->disconnect(this);  // kendi "disconnected" slotumuz tetiklenmesin
        socket->abort();
        socket->deleteLater();
    }
    m_clients.clear();
}

void ResultPublisher::enableUdp(const QHostAddress& host, quint16 port)
{
    m_udpHost = host;
    m_udpPort = port;
    m_udpEnabled = true;
}

void ResultPublisher::disableUdp()
{
    m_udpEnabled = false;
}

void ResultPublisher::onNewConnection()
{
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        const QString peer = peerName(socket);
        m_clients.append(socket);

        connect(socket, &QTcpSocket::disconnected, this, [this, socket, peer] {
            m_clients.removeOne(socket);
            // Sinyal işlenirken nesneyi doğrudan silmek tehlikeli (çağrı yığınında hâlâ
            // socket'in kodu olabilir). deleteLater, olay döngüsüne dönülünce siler.
            socket->deleteLater();
            emit clientDisconnected(peer);
        });
        // İstemciden gelen veriyi kullanmıyoruz; tamponda birikmesin.
        connect(socket, &QTcpSocket::readyRead, socket, [socket] { socket->readAll(); });

        emit clientConnected(peer);
    }
}

void ResultPublisher::publish(const InspectionResult& result)
{
    if (m_clients.isEmpty() && !m_udpEnabled) {
        return;  // dinleyen yok: serileştirme maliyetine bile girme
    }
    const QByteArray message =
        protocol::encode(protocol::fromResult(result, QDateTime::currentDateTimeUtc()));

    for (QTcpSocket* socket : std::as_const(m_clients)) {
        if (socket->bytesToWrite() > kMaxPendingBytesPerClient) {
            ++m_skippedSlow;  // istemci okumuyor/yavaş: bu mesajı ona atla
            continue;
        }
        socket->write(message);  // bloklamaz: tampona yazar
    }

    if (m_udpEnabled) {
        if (m_udp.writeDatagram(message, m_udpHost, m_udpPort) < 0) {
            emit errorOccurred(tr("UDP send failed: %1").arg(m_udp.errorString()));
        }
    }
    ++m_published;
}

}  // namespace inspector
