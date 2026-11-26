#include "inspector/DefectDetector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

namespace inspector {

namespace {

bool isPositiveOdd(int value)
{
    return value > 0 && value % 2 == 1;
}

cv::Mat toGray(const cv::Mat& image)
{
    switch (image.type()) {
    case CV_8UC1:
        return image;  // kopya yok: cv::Mat referans sayımlı, sadece başlık kopyalanır
    case CV_8UC3: {
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }
    case CV_8UC4: {
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        return gray;
    }
    default:
        throw std::invalid_argument("DefectDetector: unsupported image type " +
                                    std::to_string(image.type()));
    }
}

}  // namespace

DefectDetector::DefectDetector(DetectorConfig config) : m_config(config)
{
    validate(m_config);
}

void DefectDetector::setConfig(const DetectorConfig& config)
{
    validate(config);  // önce doğrula: geçersizse eski ayar bozulmadan kalsın
    m_config = config;
}

void DefectDetector::validate(const DetectorConfig& config)
{
    if (!isPositiveOdd(config.blurKernelSize)) {
        throw std::invalid_argument("blurKernelSize must be a positive odd number");
    }
    if (!isPositiveOdd(config.backgroundKernelSize)) {
        throw std::invalid_argument("backgroundKernelSize must be a positive odd number");
    }
    if (config.threshold < 0 || config.threshold > 255) {
        throw std::invalid_argument("threshold must be in [0, 255]");
    }
    if (config.minDefectArea < 0.0) {
        throw std::invalid_argument("minDefectArea must be >= 0");
    }
}

DetectionResult DefectDetector::detect(const cv::Mat& image) const
{
    if (image.empty()) {
        throw std::invalid_argument("DefectDetector: empty image");
    }
    const auto start = std::chrono::steady_clock::now();

    // 1-2. Gri ton + gürültü bastırma
    const cv::Mat gray = toGray(image);
    cv::Mat smoothed;
    const cv::Size blurSize(m_config.blurKernelSize, m_config.blurKernelSize);
    cv::GaussianBlur(gray, smoothed, blurSize, 0);

    // 3. Yerel arka plan. Box filtre, pencere boyutundan bağımsız O(1)/piksel çalışır.
    cv::Mat background;
    const cv::Size bgSize(m_config.backgroundKernelSize, m_config.backgroundKernelSize);
    cv::blur(smoothed, background, bgSize, cv::Point(-1, -1), cv::BORDER_REFLECT);

    // 4. Arka plandan mutlak sapma + eşikleme
    cv::Mat diff;
    cv::absdiff(smoothed, background, diff);
    cv::Mat mask;
    cv::threshold(diff, mask, m_config.threshold, 255, cv::THRESH_BINARY);

    // 5. Morfolojik açma (erode + dilate): küçük kırıntıları sil
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);

    // 6. Konturlar
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    DetectionResult result;
    result.defects.reserve(contours.size());
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < m_config.minDefectArea) {
            continue;
        }
        const cv::Moments m = cv::moments(contour);
        Defect defect;
        defect.boundingBox = cv::boundingRect(contour);
        defect.area = area;
        defect.center = cv::Point2f(static_cast<float>(m.m10 / m.m00),
                                    static_cast<float>(m.m01 / m.m00));
        result.defects.push_back(defect);
    }

    std::sort(result.defects.begin(), result.defects.end(),
              [](const Defect& a, const Defect& b) { return a.area > b.area; });

    const auto elapsed = std::chrono::steady_clock::now() - start;
    result.processingMs = std::chrono::duration<double, std::milli>(elapsed).count();
    return result;
}

void DefectDetector::drawDefects(cv::Mat& image, const DetectionResult& result)
{
    const cv::Scalar red(0, 0, 255);  // OpenCV renk sırası BGR'dir
    for (const auto& defect : result.defects) {
        cv::rectangle(image, defect.boundingBox, red, 2);
    }
}

}  // namespace inspector
