#include "inspector/ImageConversion.h"

namespace inspector {

QImage matToQImage(const cv::Mat& mat)
{
    // Bu QImage constructor'ı veriyi KOPYALAMAZ, sadece Mat'in belleğini gösterir.
    // Mat serbest bırakıldığında QImage geçersiz belleği gösterirdi; copy() bunu önler.
    const auto stride = static_cast<qsizetype>(mat.step);
    switch (mat.type()) {
    case CV_8UC3:
        return QImage(mat.data, mat.cols, mat.rows, stride, QImage::Format_BGR888).copy();
    case CV_8UC1:
        return QImage(mat.data, mat.cols, mat.rows, stride, QImage::Format_Grayscale8).copy();
    default:
        return {};
    }
}

}  // namespace inspector
