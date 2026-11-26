#pragma once

#include <opencv2/core/mat.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace inspector {

/// Kaynaktan okunan tek bir kare.
struct Frame {
    cv::Mat image;  // BGR, 8 bit
    std::uint64_t index = 0;  // kaynağın açılışından itibaren sıra numarası
    std::chrono::steady_clock::time_point timestamp;
};

/// Görüntü kaynağı soyutlaması (kamera, video dosyası, görüntü klasörü).
///
/// Uygulamanın geri kalanı (capture thread'i) yalnızca bu arayüzü bilir; kaynağın
/// türü çalışma zamanında seçilir (polimorfizm). Yeni bir kaynak (ör. GigE endüstriyel
/// kamera SDK'sı) eklemek için yalnızca yeni bir alt sınıf yazmak yeter.
///
/// NVI (Non-Virtual Interface) deseni: Dışarıya açık next() sanal DEĞİLDİR; ortak işleri
/// (sıra numarası, zaman damgası, boş kare kontrolü) tek yerde yapar ve alt sınıfa
/// özgü kısmı korumalı sanal readImage()'a devreder.
///
/// RAII: Kaynaklar constructor'da açılır, açılamazsa std::runtime_error fırlatılır;
/// destructor'da otomatik kapanır. "Yarı açık" nesne diye bir durum yoktur.
class FrameSource {
public:
    virtual ~FrameSource() = default;  // taban sınıf: sanal destructor şart

    FrameSource(const FrameSource&) = delete;
    FrameSource& operator=(const FrameSource&) = delete;

    /// Sıradaki kareyi döndürür. Akış bittiyse ya da okunamadıysa std::nullopt.
    std::optional<Frame> next();

    /// Kullanıcıya gösterilecek kısa açıklama, ör. "camera:0" ya da dosya yolu.
    virtual std::string description() const = 0;

    /// Canlı kaynak (kamera) kendi hızında kare üretir; dosya kaynakları ise
    /// capture thread'i tarafından fps()'e göre yavaşlatılmalıdır.
    virtual bool isLive() const noexcept = 0;

    /// Hedef kare hızı. Bilinmiyorsa 0.
    virtual double fps() const noexcept = 0;

    /// Bağlantısı kopabilen kaynaklar (kamera) true döner. next() başarısız olunca
    /// capture thread'i reopen() ile kaynağı yeniden açmayı dener. Dosya kaynaklarında
    /// akışın bitmesi normaldir, yeniden açılmaz.
    virtual bool canReopen() const noexcept { return false; }

    /// Kaynağı kapatıp yeniden açar. Başarılıysa true. Sıra numaraları devam eder.
    virtual bool reopen() { return false; }

protected:
    FrameSource() = default;

    /// Alt sınıflar bir kare okuyup `out`a yazar; akış bittiyse false döner.
    virtual bool readImage(cv::Mat& out) = 0;

private:
    std::uint64_t m_nextIndex = 0;
};

/// `spec` metnine göre uygun kaynağı oluşturur (fabrika fonksiyonu):
///   * yalnızca rakamlardan oluşuyorsa ("0", "1") -> kamera cihaz numarası
///   * bir klasörse                               -> görüntü klasörü
///   * aksi halde                                 -> video dosyası
/// `loop` dosya kaynaklarının sona gelince başa sarıp sarmayacağını belirler.
std::unique_ptr<FrameSource> createFrameSource(const std::string& spec, bool loop = true);

}  // namespace inspector
