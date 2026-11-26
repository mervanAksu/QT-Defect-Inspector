#pragma once

#include "inspector/LineBuffer.h"
#include "inspector/Protocol.h"

#include <QHostAddress>
#include <QMetaType>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace inspector {

/// Defect Inspector'ın yayınladığı JSON Lines mesajlarını alır.
///
///   * TCP modu: sunucuya bağlanır. Bağlantı kurulamazsa ya da koparsa üstel geri
///     çekilme (exponential backoff) ile yeniden dener: 1 s, 2 s, 4 s ... en fazla 30 s.
///     Başarılı bağlantıda bekleme süresi başa döner.
///     Neden sabit aralık değil? Sunucu çöktüğünde yüzlerce istemcinin aynı anda
///     saniyede bir bağlanmaya çalışması toparlanmayı zorlaştırır ("thundering herd").
///   * UDP modu: bir portu dinler, her datagram bir mesajdır. Bağlantı kavramı yoktur.
///
/// Olay tabanlıdır: hiçbir çağrı bloklamaz, her şey Qt olay döngüsünde ilerler.
class ReceiverClient : public QObject {
    Q_OBJECT

public:
    struct Stats {
        quint64 received = 0;   // geçerli mesaj
        quint64 defective = 0;  // en az bir kusur içeren mesaj
        quint64 invalid = 0;    // çözülemeyen ya da çok uzun satır
        quint64 reconnectAttempts = 0;
    };

    explicit ReceiverClient(QObject* parent = nullptr);
    ~ReceiverClient() override;

    /// Yeniden bağlanma bekleme süreleri (testlerde kısaltmak için).
    void setReconnectDelays(int initialMs, int maxMs);

    void connectToServer(const QString& host, quint16 port);
    bool listenUdp(quint16 port, const QHostAddress& address = QHostAddress::Any);
    /// Bağlantıyı kapatır ve bekleyen yeniden bağlanma denemesini iptal eder.
    void stop();

    bool isConnected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }
    quint16 udpPort() const { return m_udp.localPort(); }
    const Stats& stats() const noexcept { return m_stats; }

signals:
    void connected();
    void disconnected();
    void reconnectScheduled(int delayMs);
    void messageReceived(const inspector::protocol::Message& message);
    void invalidMessage(const QString& error);
    void errorOccurred(const QString& message);

private:
    void attemptConnection();
    void scheduleReconnect();
    void onStateChanged(QAbstractSocket::SocketState state);
    void onTcpReadyRead();
    void onUdpReadyRead();
    void handleLine(const QByteArray& line);

    QTcpSocket m_socket;
    QUdpSocket m_udp;
    LineBuffer m_buffer;
    QTimer m_reconnectTimer;
    QString m_host;
    quint16 m_port = 0;
    int m_initialDelayMs = 1000;
    int m_maxDelayMs = 30000;
    int m_nextDelayMs = 1000;
    bool m_active = false;        // TCP modunda çalışıyoruz (stop() çağrılmadı)
    bool m_wasConnected = false;  // "disconnected" sinyalini yalnızca gerçekten bağlıyken yay
    Stats m_stats;
};

}  // namespace inspector

Q_DECLARE_METATYPE(inspector::protocol::Message)
