#pragma once

#include "inspector/Protocol.h"

#include <QByteArray>

#include <optional>

namespace inspector {

/// TCP bayt akışını '\n' ile ayrılmış satırlara böler (mesaj çerçeveleme / framing).
///
/// Tipik hata: "her readyRead bir mesajdır" sanmak. Gerçekte bir okuma
///   * yarım bir mesaj ("{\"type\":\"insp"),
///   * birden fazla mesaj ("...}\n{...}\n{..."),
///   * ya da ikisinin karışımını
/// döndürebilir. Bu sınıf gelen parçaları biriktirir ve yalnızca tamamlanmış satırları verir.
///
/// `maxLineLength`'ten uzun satırlar atılır (bellek koruması) ve sayılır.
class LineBuffer {
public:
    explicit LineBuffer(qsizetype maxLineLength = protocol::kMaxMessageBytes);

    void append(const QByteArray& data);

    /// Sıradaki tam satırı ('\n' ve varsa '\r' olmadan) döndürür; yoksa std::nullopt.
    std::optional<QByteArray> nextLine();

    qsizetype pendingBytes() const noexcept { return m_buffer.size(); }
    qsizetype discardedLines() const noexcept { return m_discarded; }

private:
    QByteArray m_buffer;
    qsizetype m_maxLineLength;
    qsizetype m_discarded = 0;
    bool m_skipping = false;  // aşırı uzun bir satırın kalanını atlıyoruz
};

}  // namespace inspector
