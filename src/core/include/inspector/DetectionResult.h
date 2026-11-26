#pragma once

#include <opencv2/core/types.hpp>

#include <vector>

namespace inspector {

/// Tek bir kusur bölgesi (piksel koordinatlarında).
struct Defect {
    cv::Rect boundingBox;
    cv::Point2f center;
    double area = 0.0;  // kontur alanı, piksel²
};

/// Bir karenin analiz sonucu. Kusurlar alana göre büyükten küçüğe sıralıdır.
struct DetectionResult {
    std::vector<Defect> defects;
    double processingMs = 0.0;

    bool hasDefects() const noexcept { return !defects.empty(); }
};

}  // namespace inspector
