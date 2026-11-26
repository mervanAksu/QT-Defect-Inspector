#pragma once

#include "inspector/Protocol.h"

#include <QString>

namespace inspector {

/// Tek satırlık okunur özet, ör.:
///   "#17     2 defect(s)  largest 292 px² @ (483,295)  latency 2.1 ms"
///   "#18     OK                                        latency 1.9 ms"
/// Python istemcisi (python/receiver.py) aynı biçimi üretir.
inline QString formatMessage(const protocol::Message& m)
{
    const QString frame = QStringLiteral("#%1").arg(m.frameIndex, -6);
    const QString latency = QStringLiteral("latency %1 ms").arg(m.latencyMs, 0, 'f', 1);
    if (m.defectCount == 0) {
        return QStringLiteral("%1 OK  %2").arg(frame, latency);
    }
    QString detail = QStringLiteral("%1 defect(s)").arg(m.defectCount);
    if (!m.defects.isEmpty()) {
        // Yayıncı kusurları alana göre büyükten küçüğe sıralı gönderir, ama alıcı buna
        // güvenmesin: en büyüğünü kendisi bulsun.
        const protocol::DefectInfo* largest = &m.defects.front();
        for (const auto& d : m.defects) {
            if (d.area > largest->area) {
                largest = &d;
            }
        }
        detail += QStringLiteral("  largest %1 px² @ (%2,%3)")
                      .arg(largest->area, 0, 'f', 0)
                      .arg(largest->center.x(), 0, 'f', 0)
                      .arg(largest->center.y(), 0, 'f', 0);
    }
    return QStringLiteral("%1 %2  %3").arg(frame, detail, latency);
}

}  // namespace inspector
