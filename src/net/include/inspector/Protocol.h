#pragma once

#include "inspector/InspectionResult.h"

#include <QByteArray>
#include <QDateTime>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

/// Tespit sonuçlarının ağ protokolü: JSON Lines (NDJSON).
///
///   * TCP: her mesaj tek satırlık, sıkıştırılmış bir JSON nesnesi + '\n'.
///     TCP bir bayt AKIŞIDIR, mesaj sınırı yoktur: bir read() yarım mesaj ya da
///     birden fazla mesaj döndürebilir. Sınırları '\n' belirler (bkz. LineBuffer).
///   * UDP: her datagram tek bir mesajdır (datagram sınırlarını UDP korur).
///
/// Örnek:
///   {"type":"inspection","version":1,"frame":17,"sent_at":"2026-11-26T14:44:56.759Z",
///    "defect_count":1,"processing_ms":2.1,"latency_ms":3.0,"dropped_frames":0,
///    "image":{"width":640,"height":480},
///    "defects":[{"x":51,"y":70,"width":20,"height":19,"cx":61.2,"cy":79.5,"area":271}]}
///
/// Görüntünün kendisi gönderilmez; yalnızca metadata. Böylece mesajlar küçük kalır.
namespace inspector::protocol {

inline constexpr int kVersion = 1;
inline constexpr const char* kTypeInspection = "inspection";
/// Bir mesajın en fazla bu kadar kusur listelemesine izin verilir (UDP datagram'ı
/// küçük kalsın). defect_count her zaman gerçek toplamı taşır.
inline constexpr int kMaxListedDefects = 50;
/// Alıcıların kabul edeceği en uzun satır; kötü niyetli/bozuk bir gönderici
/// alıcının belleğini sınırsızca doldurmasın.
inline constexpr int kMaxMessageBytes = 64 * 1024;

struct DefectInfo {
    QRect box;
    QPointF center;
    double area = 0.0;
};

struct Message {
    int version = kVersion;
    quint64 frameIndex = 0;
    QDateTime sentAt;
    int defectCount = 0;  // toplam; `defects` en fazla kMaxListedDefects eleman taşır
    double processingMs = 0.0;
    double latencyMs = 0.0;
    quint64 droppedFrames = 0;
    QSize imageSize;
    QVector<DefectInfo> defects;
};

Message fromResult(const InspectionResult& result, const QDateTime& sentAt);

/// Sıkıştırılmış tek satır JSON + '\n'.
QByteArray encode(const Message& message);

/// Bir satırı çözer. Geçersizse std::nullopt döner ve `error` doldurulur.
std::optional<Message> decode(const QByteArray& line, QString* error = nullptr);

}  // namespace inspector::protocol
