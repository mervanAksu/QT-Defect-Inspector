#include "SampleGenerator.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

namespace inspector::samples {

namespace {

constexpr int kGridCols = 3;
constexpr int kGridRows = 2;

/// Dokuma deseni + yavaş değişen aydınlatma, gri tonda (CV_32F).
cv::Mat makeTexture(cv::Size size, std::mt19937& rng)
{
    std::uniform_real_distribution<float> phase(0.0f, 6.28f);
    const float px = phase(rng);
    const float py = phase(rng);
    constexpr float kTwoPi = 6.2831853f;
    constexpr float kPeriod = 5.0f;  // iplik aralığı, piksel

    cv::Mat gray(size, CV_32F);
    const float cx = static_cast<float>(size.width) / 2.0f;
    const float cy = static_cast<float>(size.height) / 2.0f;
    for (int y = 0; y < size.height; ++y) {
        auto* row = gray.ptr<float>(y);
        for (int x = 0; x < size.width; ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const float weave = 12.0f * std::cos(kTwoPi * fx / kPeriod + px) +
                                12.0f * std::cos(kTwoPi * fy / kPeriod + py);
            // Soldan sağa hafif gradyan + kenarlara doğru kararma (vinyet).
            const float gradient = 30.0f * (fx / static_cast<float>(size.width) - 0.5f);
            const float r2 = ((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) / (cx * cx + cy * cy);
            const float vignette = -25.0f * r2;
            row[x] = 150.0f + weave + gradient + vignette;
        }
    }
    return gray;
}

/// Hücre içinde, kenarlardan `margin` uzakta rastgele bir merkez.
cv::Point randomCenterInCell(int cell, cv::Size size, int margin, std::mt19937& rng)
{
    const int cellW = size.width / kGridCols;
    const int cellH = size.height / kGridRows;
    const int col = cell % kGridCols;
    const int row = cell / kGridCols;
    std::uniform_int_distribution<int> dx(margin, cellW - margin);
    std::uniform_int_distribution<int> dy(margin, cellH - margin);
    return {col * cellW + dx(rng), row * cellH + dy(rng)};
}

/// `mask` (0-1, CV_32F) bölgesini `delta` kadar koyulaştırır/açar.
void applyDelta(cv::Mat& gray, const cv::Mat& mask, float delta)
{
    gray += mask * delta;
}

cv::Rect addDefect(cv::Mat& gray, DefectKind kind, cv::Point center, std::mt19937& rng)
{
    cv::Mat mask = cv::Mat::zeros(gray.size(), CV_32F);
    switch (kind) {
    case DefectKind::Stain: {
        // Kenarları yumuşak, koyu, eliptik yağ lekesi.
        std::uniform_int_distribution<int> radius(10, 20);
        std::uniform_real_distribution<double> angle(0.0, 180.0);
        const cv::Size axes(radius(rng), radius(rng));
        cv::ellipse(mask, center, axes, angle(rng), 0, 360, cv::Scalar(1.0), cv::FILLED);
        cv::GaussianBlur(mask, mask, cv::Size(0, 0), 3.0);
        applyDelta(gray, mask, -90.0f);
        break;
    }
    case DefectKind::Hole: {
        // Delik: arkadaki ışık görünür, keskin kenarlı parlak daire.
        std::uniform_int_distribution<int> radius(6, 10);
        cv::circle(mask, center, radius(rng), cv::Scalar(1.0), cv::FILLED, cv::LINE_AA);
        applyDelta(gray, mask, +95.0f);
        break;
    }
    case DefectKind::Tear: {
        // Yırtık: rastgele açılı, kalın koyu çizgi.
        std::uniform_int_distribution<int> length(40, 70);
        std::uniform_real_distribution<double> angle(0.0, 3.14159);
        const double a = angle(rng);
        const double half = length(rng) / 2.0;
        const cv::Point offset(static_cast<int>(std::cos(a) * half),
                               static_cast<int>(std::sin(a) * half));
        cv::line(mask, center - offset, center + offset, cv::Scalar(1.0), 4, cv::LINE_AA);
        applyDelta(gray, mask, -90.0f);
        break;
    }
    }
    return cv::boundingRect(mask > 0.05);
}

}  // namespace

GeneratedSample generateFabric(const std::vector<DefectKind>& defects, unsigned seed,
                               cv::Size size)
{
    if (defects.size() > static_cast<std::size_t>(kGridCols * kGridRows)) {
        throw std::invalid_argument("generateFabric: at most 6 defects per image");
    }
    std::mt19937 rng(seed);
    cv::Mat gray = makeTexture(size, rng);

    // Her kusura ayrı bir ızgara hücresi: kusurlar birbirine değmesin.
    std::vector<int> cells(kGridCols * kGridRows);
    std::iota(cells.begin(), cells.end(), 0);
    std::shuffle(cells.begin(), cells.end(), rng);

    GeneratedSample sample;
    for (std::size_t i = 0; i < defects.size(); ++i) {
        const cv::Point center = randomCenterInCell(cells[i], size, 50, rng);
        sample.defectBoxes.push_back(addDefect(gray, defects[i], center, rng));
    }

    // Sensör gürültüsü (sabit tohumlu).
    cv::Mat noise(size, CV_32F);
    cv::RNG cvRng(seed);
    cvRng.fill(noise, cv::RNG::NORMAL, 0.0, 5.0);
    gray += noise;

    cv::Mat gray8;
    gray.convertTo(gray8, CV_8U);  // 0-255'e kırpar

    // Bej kumaş tonu: BGR kanallarını farklı ölçekle.
    cv::Mat channels[3];
    gray8.convertTo(channels[0], CV_8U, 0.80);  // B
    gray8.convertTo(channels[1], CV_8U, 0.93);  // G
    gray8.convertTo(channels[2], CV_8U, 1.00);  // R
    cv::merge(channels, 3, sample.image);
    return sample;
}

std::vector<SampleSpec> defaultSampleSet()
{
    using K = DefectKind;
    return {
        {"clean", {}},
        {"stain", {K::Stain}},
        {"hole", {K::Hole}},
        {"tear", {K::Tear}},
        {"clean", {}},
        {"stain_hole", {K::Stain, K::Hole}},
        {"tear_stain", {K::Tear, K::Stain}},
        {"clean", {}},
        {"two_holes", {K::Hole, K::Hole}},
        {"stain_tear_hole", {K::Stain, K::Tear, K::Hole}},
        {"clean", {}},
        {"stain", {K::Stain}},
    };
}

std::string toString(DefectKind kind)
{
    switch (kind) {
    case DefectKind::Stain: return "stain";
    case DefectKind::Hole: return "hole";
    case DefectKind::Tear: return "tear";
    }
    return "unknown";
}

}  // namespace inspector::samples
