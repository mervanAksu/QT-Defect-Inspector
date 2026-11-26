#pragma once

#include "inspector/FrameSource.h"
#include "inspector/ThreadSafeQueue.h"

#include <QThread>

#include <memory>

namespace inspector {

/// Üretici (producer): kaynaktan kare okuyup kuyruğa koyar.
///
/// QThread'den türetip run()'ı override ediyoruz, çünkü bu thread'in işi olay
/// (event) beklemek değil, sürekli dönen bir okuma döngüsü. Olay döngüsü gerekmez.
/// (Diğer yöntem — bir QObject worker'ı moveToThread() ile taşımak — slot'larla
/// olay tabanlı çalışan işler için daha uygundur.)
///
/// Bitiş: kaynak biterse ya da requestInterruption() çağrılırsa döngüden çıkar ve
/// kuyruğu kapatır; böylece tüketici "başka kare gelmeyecek" bilgisini alır.
class CaptureThread : public QThread {
    Q_OBJECT

public:
    CaptureThread(std::unique_ptr<FrameSource> source,
                  std::shared_ptr<ThreadSafeQueue<Frame>> queue, QObject* parent = nullptr);

    /// Çalışan bir QThread nesnesini yok etmek programı çökertir; bu yüzden
    /// destructor önce thread'i durdurup bitmesini bekler.
    ~CaptureThread() override;

signals:
    void sourceExhausted();
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    // Kaynak yalnızca run() içinde, yani capture thread'inde kullanılır.
    std::unique_ptr<FrameSource> m_source;
    std::shared_ptr<ThreadSafeQueue<Frame>> m_queue;
};

}  // namespace inspector
