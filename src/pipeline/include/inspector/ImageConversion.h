#pragma once

#include <QImage>

#include <opencv2/core/mat.hpp>

namespace inspector {

/// cv::Mat (CV_8UC3 BGR ya da CV_8UC1) -> QImage. Piksel verisi DERİN kopyalanır,
/// yani dönen QImage, Mat yok edildikten sonra da geçerlidir. Desteklenmeyen
/// formatlarda boş (isNull) QImage döner.
QImage matToQImage(const cv::Mat& mat);

}  // namespace inspector
