#include "ReceiverClient.h"

#include <QNetworkDatagram>

#include <algorithm>

namespace inspector {

ReceiverClient::ReceiverClient(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<protocol::Message>();

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &ReceiverClient::attemptConnection);

    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_wasConnected = true;
        m_nextDelayMs = m_initialDelayMs;  // başarılı bağlantı: geri çekilmeyi sıfırla
        emit connected();
    });
    connect(&m_socket, &QTcpSocket::stateChanged, this, &ReceiverClient::onStateChanged);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_active) {
            emit errorOccurred(m_socket.errorString());
        }
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &ReceiverClient::onTcpReadyRead);
    connect(&m_udp, &QUdpSocket::readyRead, this, &ReceiverClient::onUdpReadyRead);
}

ReceiverClient::~ReceiverClient()
{
    stop();
}

void ReceiverClient::setReconnectDelays(int initialMs, int maxMs)
{
    m_initialDelayMs = std::max(1, initialMs);
    m_maxDelayMs = std::max(m_initialDelayMs, maxMs);
    m_nextDelayMs = m_initialDelayMs;
}

void ReceiverClient::connectToServer(const QString& host, quint16 port)
{
    stop();
    m_host = host;
    m_port = port;
    m_active = true;
    m_nextDelayMs = m_initialDelayMs;
    attemptConnection();
}

bool ReceiverClient::listenUdp(quint16 port, const QHostAddress& address)
{
    stop();
    if (!m_udp.bind(address, port)) {
        emit errorOccurred(tr("Cannot bind UDP port %1: %2").arg(port).arg(m_udp.errorString()));
        return false;
    }
    return true;
}

void ReceiverClient::stop()
{
    m_active = false;  // önce: abort() tetikleyeceği durum değişikliği yeniden bağlanmasın
    m_wasConnected = false;  // bizim kapatmamız "bağlantı koptu" (disconnected) sayılmaz
    m_reconnectTimer.stop();
    m_socket.abort();
    m_udp.close();
}

void ReceiverClient::attemptConnection()
{
    if (!m_active) {
        return;
    }
    m_buffer = LineBuffer();  // önceki bağlantıdan kalan yarım satır yeni akışa karışmasın
    m_socket.connectToHost(m_host, m_port);
}

void ReceiverClient::onStateChanged(QAbstractSocket::SocketState state)
{
    if (state != QAbstractSocket::UnconnectedState) {
        return;
    }
    // Hem "bağlantı kurulamadı" hem "kurulu bağlantı koptu" bu duruma düşer;
    // yeniden bağlanmayı tek bir yerden planlamak çift tetiklenmeyi önler.
    if (m_wasConnected) {
        m_wasConnected = false;
        emit disconnected();
    }
    // Qt, bağlantı hatasında önce stateChanged'i, SONRA errorOccurred'ı yayınlar.
    // Planlamayı olay döngüsüne erteliyoruz ki önce hata, sonra "reconnecting in"
    // raporlansın. O ana kadar durum değişmiş olabilir (stop(), yeni bağlantı),
    // bu yüzden koşulları çalışma anında tekrar kontrol ediyoruz.
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (m_active && !m_reconnectTimer.isActive() &&
                m_socket.state() == QAbstractSocket::UnconnectedState) {
                scheduleReconnect();
            }
        },
        Qt::QueuedConnection);
}

void ReceiverClient::scheduleReconnect()
{
    ++m_stats.reconnectAttempts;
    emit reconnectScheduled(m_nextDelayMs);
    m_reconnectTimer.start(m_nextDelayMs);
    m_nextDelayMs = std::min(m_nextDelayMs * 2, m_maxDelayMs);
}

void ReceiverClient::onTcpReadyRead()
{
    const qsizetype discardedBefore = m_buffer.discardedLines();
    m_buffer.append(m_socket.readAll());
    while (const auto line = m_buffer.nextLine()) {
        handleLine(*line);
    }
    const auto newlyDiscarded = static_cast<quint64>(m_buffer.discardedLines() - discardedBefore);
    if (newlyDiscarded > 0) {
        m_stats.invalid += newlyDiscarded;
        emit invalidMessage(tr("line longer than %1 bytes discarded").arg(protocol::kMaxMessageBytes));
    }
}

void ReceiverClient::onUdpReadyRead()
{
    while (m_udp.hasPendingDatagrams()) {
        handleLine(m_udp.receiveDatagram().data());
    }
}

void ReceiverClient::handleLine(const QByteArray& line)
{
    if (line.trimmed().isEmpty()) {
        return;
    }
    QString error;
    const auto message = protocol::decode(line, &error);
    if (!message) {
        ++m_stats.invalid;
        emit invalidMessage(error);  // bozuk bir mesaj bağlantıyı kesmez
        return;
    }
    ++m_stats.received;
    if (message->defectCount > 0) {
        ++m_stats.defective;
    }
    emit messageReceived(*message);
}

}  // namespace inspector
