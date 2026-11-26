#pragma once

#include "inspector/FrameSource.h"
#include "inspector/ThreadSafeQueue.h"

#include <QThread>

#include <memory>

namespace inspector {

/// Kopan bir canlı kaynağı (kamera) yeniden açma denemelerinin bekleme süreleri.
struct ReconnectPolicy {
    int initialDelayMs = 250;
    int maxDelayMs = 4000;
};

/// Üretici (producer): kaynaktan kare okuyup kuyruğa koyar.
///
/// QThread'den türetip run()'ı override ediyoruz, çünkü bu thread'in işi olay
/// (event) beklemek değil, sürekli dönen bir okuma döngüsü. Olay döngüsü gerekmez.
/// (Diğer yöntem — bir QObject worker'ı moveToThread() ile taşımak — slot'larla
/// olay tabanlı çalışan işler için daha uygundur.)
///
/// Bitiş: kaynak biterse ya da requestInterruption() çağrılırsa döngüden çıkar ve
/// kuyruğu kapatır; böylece tüketici "başka kare gelmeyecek" bilgisini alır.
///
/// Kopma: canlı bir kaynak (canReopen()) kare veremezse thread bitmez; sourceLost
/// yayınlar ve kaynağı üstel geri çekilmeyle yeniden açmayı dener. Başarınca
/// sourceRecovered yayınlar ve kaldığı yerden devam eder.
class CaptureThread : public QThread {
    Q_OBJECT

public:
    CaptureThread(std::unique_ptr<FrameSource> source,
                  std::shared_ptr<ThreadSafeQueue<Frame>> queue, ReconnectPolicy policy = {},
                  QObject* parent = nullptr);

    /// Çalışan bir QThread nesnesini yok etmek programı çökertir; bu yüzden
    /// destructor önce thread'i durdurup bitmesini bekler.
    ~CaptureThread() override;

signals:
    void sourceExhausted();
    void sourceLost(const QString& source);
    void sourceRecovered(int attempts);
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    bool recoverSource();
    /// `ms` kadar uyur ama durdurma istenirse hemen döner (false).
    bool sleepInterruptibly(int ms);

    // Kaynak yalnızca run() içinde, yani capture thread'inde kullanılır.
    std::unique_ptr<FrameSource> m_source;
    std::shared_ptr<ThreadSafeQueue<Frame>> m_queue;
    const ReconnectPolicy m_policy;
};

}  // namespace inspector
