#include "inspector/LineBuffer.h"

namespace inspector {

LineBuffer::LineBuffer(qsizetype maxLineLength) : m_maxLineLength(maxLineLength) {}

void LineBuffer::append(const QByteArray& data)
{
    m_buffer.append(data);
    // Hiç '\n' yokken sınır aşıldıysa bu satır asla kabul edilmeyecek: belleği
    // boşalt ve satırın geri kalanını bir sonraki '\n'e kadar atla.
    if (m_buffer.size() > m_maxLineLength && !m_buffer.contains('\n')) {
        if (!m_skipping) {
            ++m_discarded;
            m_skipping = true;
        }
        m_buffer.clear();
    }
}

std::optional<QByteArray> LineBuffer::nextLine()
{
    for (;;) {
        const qsizetype newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            return std::nullopt;  // tamamlanmış satır yok; daha fazla veri bekle
        }
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);

        if (m_skipping) {  // aşırı uzun satırın kuyruğu
            m_skipping = false;
            continue;
        }
        if (line.size() > m_maxLineLength) {
            ++m_discarded;
            continue;
        }
        if (line.endsWith('\r')) {
            line.chop(1);  // "\r\n" gönderen istemcilere tolerans
        }
        return line;
    }
}

}  // namespace inspector
