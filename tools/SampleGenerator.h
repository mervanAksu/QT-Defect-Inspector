#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace inspector::samples {

enum class DefectKind { Stain, Hole, Tear };

/// Bir örnek görüntünün tarifi: dosya adı eki ve içereceği kusurlar.
struct SampleSpec {
    std::string name;
    std::vector<DefectKind> defects;
};

/// Üretilen görüntü + yer gerçeği (ground truth): her kusurun yaklaşık kutusu.
struct GeneratedSample {
    cv::Mat image;  // BGR, 8 bit
    std::vector<cv::Rect> defectBoxes;
};

/// Sentetik kumaş üretir: dokuma deseni + düzensiz aydınlatma + sensör gürültüsü,
/// üzerine istenen kusurlar. Aynı `seed` her zaman aynı görüntüyü verir.
/// Kusurlar 3x2'lik ızgaranın farklı hücrelerine konur, birbirine değmez (en fazla 6).
GeneratedSample generateFabric(const std::vector<DefectKind>& defects, unsigned seed,
                               cv::Size size = {640, 480});

/// samples/fabric klasöründeki 12 görüntülük varsayılan set.
std::vector<SampleSpec> defaultSampleSet();

std::string toString(DefectKind kind);

}  // namespace inspector::samples
