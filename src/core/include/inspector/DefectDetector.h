#pragma once

#include "inspector/DetectionResult.h"

#include <opencv2/core/mat.hpp>

namespace inspector {

/// Dedektör ayarları. GUI'deki eşik slider'ı `threshold` alanını değiştirecek.
struct DetectorConfig {
    int blurKernelSize = 5;          // gürültü bastırma (tek sayı olmalı)
    int backgroundKernelSize = 101;  // yerel arka plan tahmini penceresi (tek sayı)
    int threshold = 60;              // arka plandan sapma eşiği, 0-255
    double minDefectArea = 30.0;     // bundan küçük bölgeler gürültü sayılır, piksel²
};

/// Kumaş gibi düzgün yüzeylerde leke / delik / yırtık tespiti.
///
/// Algoritma:
///   1. Gri tonlamaya çevir
///   2. Gaussian blur ile ince doku ve sensör gürültüsünü bastır
///   3. Büyük bir pencerede ortalama alarak *yerel arka planı* tahmin et
///   4. |görüntü - arka plan| farkını eşikle  -> hem koyu leke hem açık delik yakalanır
///   5. Morfolojik açma ile tek piksellik kırıntıları temizle
///   6. Dış konturları bul, küçükleri ele, kalanları raporla
///
/// Neden sabit (global) eşik değil de arka plan farkı? Gerçek bir kamerada aydınlatma
/// görüntü boyunca eşit değildir; global eşik karanlık köşeleri kusur sanar.
///
/// Thread-safety: detect() const'tur ve paylaşılan değişken durumu yoktur; aynı nesne
/// birden fazla thread'den aynı anda okunabilir. setConfig() ise detect() ile aynı anda
/// çağrılmamalıdır (bunu processing thread'inde çözeceğiz).
class DefectDetector {
public:
    explicit DefectDetector(DetectorConfig config = {});

    /// 8 bit gri (CV_8UC1) ya da BGR/BGRA (CV_8UC3/CV_8UC4) görüntüyü analiz eder.
    /// Boş ya da desteklenmeyen formatta görüntüde std::invalid_argument fırlatır.
    DetectionResult detect(const cv::Mat& image) const;

    const DetectorConfig& config() const noexcept { return m_config; }
    void setConfig(const DetectorConfig& config);

    /// Kusurları görüntünün üzerine kırmızı dikdörtgenlerle çizer (GUI önizlemesi için).
    static void drawDefects(cv::Mat& image, const DetectionResult& result);

private:
    static void validate(const DetectorConfig& config);

    DetectorConfig m_config;
};

}  // namespace inspector
