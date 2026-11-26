#pragma once

#include "inspector/InspectionResult.h"

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QUdpSocket>

class QTcpSocket;

namespace inspector {

/// Tespit sonuçlarını ağa yayınlar:
///   * TCP sunucusu: bağlanan her istemciye her mesaj gönderilir (güvenilir, sıralı).
///   * UDP: tek bir hedefe (ya da yayın/broadcast adresine) datagram gönderilir
///     (bağlantısız, düşük gecikmeli; kayıp olabilir, sıra garanti değil).
///
/// Thread modeli: Qt soketleri asenkron ve olay tabanlıdır. write() bloklamaz;
/// veri tampona yazılır ve olay döngüsü gönderir. Bu yüzden ayrı bir thread'e
/// gerek yok; nesne GUI thread'inde yaşar. (Çok yüksek mesaj hızında bu nesne
/// moveToThread() ile kendi olay döngüsü olan bir thread'e taşınabilir.)
///
/// Yavaş istemci koruması: Bir istemcinin gönderilmeyi bekleyen verisi
/// kMaxPendingBytesPerClient'i aşarsa o istemciye giden mesajlar atlanır.
/// Aksi halde okumayan tek bir istemci sunucunun belleğini sınırsızca büyütürdü.
class ResultPublisher : public QObject {
    Q_OBJECT

public:
    static constexpr qint64 kMaxPendingBytesPerClient = 1 << 20;  // 1 MiB

    explicit ResultPublisher(QObject* parent = nullptr);
    ~ResultPublisher() override;

    /// `port` 0 ise işletim sistemi boş bir port seçer (bkz. tcpPort()).
    bool startTcpServer(quint16 port, const QHostAddress& address = QHostAddress::Any);
    void stopTcpServer();
    bool isTcpListening() const { return m_server.isListening(); }
    quint16 tcpPort() const { return m_server.serverPort(); }
    int clientCount() const { return static_cast<int>(m_clients.size()); }

    void enableUdp(const QHostAddress& host, quint16 port);
    void disableUdp();
    bool isUdpEnabled() const noexcept { return m_udpEnabled; }

    quint64 messagesPublished() const noexcept { return m_published; }
    quint64 messagesSkippedForSlowClients() const noexcept { return m_skippedSlow; }

public slots:
    void publish(const inspector::InspectionResult& result);

signals:
    void clientConnected(const QString& peer);
    void clientDisconnected(const QString& peer);
    void errorOccurred(const QString& message);

private:
    void onNewConnection();

    QTcpServer m_server;
    QList<QTcpSocket*> m_clients;  // sahibi m_server (Qt ebeveyni); deleteLater ile silinir
    QUdpSocket m_udp;
    QHostAddress m_udpHost;
    quint16 m_udpPort = 0;
    bool m_udpEnabled = false;
    quint64 m_published = 0;
    quint64 m_skippedSlow = 0;
};

}  // namespace inspector
