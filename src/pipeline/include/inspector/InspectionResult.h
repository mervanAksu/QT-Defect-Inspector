#pragma once

#include "inspector/DetectionResult.h"

#include <QImage>
#include <QMetaType>

#include <cstddef>
#include <cstdint>

namespace inspector {

/// Processing thread'inden GUI'ye (ve ağ yayıncısına) giden paket.
/// Sinyal parametresi olarak kopyalanır; QImage implicitly shared (copy-on-write)
/// olduğu için kopyalamak ucuzdur.
struct InspectionResult {
    std::uint64_t frameIndex = 0;
    DetectionResult detection;
    QImage annotatedImage;         // kusurları işaretlenmiş kare
    double latencyMs = 0.0;        // kare yakalandığından sonuç üretilene kadar geçen süre
    std::size_t droppedFrames = 0; // kuyruk dolduğu için atlanan toplam kare
};

}  // namespace inspector

// Queued connection'larda Qt, parametreyi QVariant benzeri bir kutuya kopyalayıp
// hedef thread'in olay kuyruğuna koyar; bunun için tipin meta-type sistemine
// tanıtılması gerekir.
Q_DECLARE_METATYPE(inspector::InspectionResult)
