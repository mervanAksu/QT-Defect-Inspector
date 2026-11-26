#include "inspector/DefectDetector.h"

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

using inspector::DefectDetector;
using inspector::DetectorConfig;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kFabricGray = 180;

/// Düzgün gri "kumaş" zemini.
cv::Mat makeFabric(int gray = kFabricGray)
{
    return cv::Mat(kHeight, kWidth, CV_8UC1, cv::Scalar(gray));
}

void addSpot(cv::Mat& image, cv::Point center, int radius, int gray)
{
    cv::circle(image, center, radius, cv::Scalar(gray), cv::FILLED);
}

}  // namespace

// --- Temel davranış ----------------------------------------------------------

TEST(DefectDetectorTest, CleanFabricHasNoDefects)
{
    const DefectDetector detector;
    const auto result = detector.detect(makeFabric());

    EXPECT_FALSE(result.hasDefects());
}

TEST(DefectDetectorTest, DetectsDarkStain)
{
    cv::Mat image = makeFabric();
    const cv::Point center(100, 120);
    addSpot(image, center, 12, 40);

    const auto result = DefectDetector{}.detect(image);

    ASSERT_EQ(result.defects.size(), 1u);
    EXPECT_TRUE(result.defects[0].boundingBox.contains(center));
    EXPECT_NEAR(result.defects[0].center.x, center.x, 2.0);
    EXPECT_NEAR(result.defects[0].center.y, center.y, 2.0);
}

TEST(DefectDetectorTest, DetectsBrightHole)
{
    cv::Mat image = makeFabric();
    addSpot(image, {200, 80}, 10, 255);  // delikten arkadaki ışık görünüyor

    const auto result = DefectDetector{}.detect(image);

    EXPECT_EQ(result.defects.size(), 1u);
}

TEST(DefectDetectorTest, ReportsMultipleDefectsSortedByArea)
{
    cv::Mat image = makeFabric();
    addSpot(image, {60, 60}, 6, 30);     // küçük
    addSpot(image, {250, 170}, 15, 30);  // büyük

    const auto result = DefectDetector{}.detect(image);

    ASSERT_EQ(result.defects.size(), 2u);
    EXPECT_GT(result.defects[0].area, result.defects[1].area);
    EXPECT_TRUE(result.defects[0].boundingBox.contains({250, 170}));
}

TEST(DefectDetectorTest, IgnoresDefectsSmallerThanMinArea)
{
    cv::Mat image = makeFabric();
    addSpot(image, {160, 120}, 2, 0);  // ~12 piksel², varsayılan minimum 30

    EXPECT_FALSE(DefectDetector{}.detect(image).hasDefects());
}

// --- Gerçek dünyaya yakın koşullar --------------------------------------------

TEST(DefectDetectorTest, SensorNoiseIsNotADefect)
{
    cv::Mat image = makeFabric();
    cv::Mat noise(image.size(), CV_16SC1);
    cv::theRNG().state = 12345;  // tekrarlanabilir test için sabit tohum
    cv::randn(noise, 0, 10);
    cv::Mat noisy;
    image.convertTo(noisy, CV_16SC1);
    noisy += noise;
    noisy.convertTo(image, CV_8UC1);  // 0-255 aralığına kırpar (saturate_cast)

    EXPECT_FALSE(DefectDetector{}.detect(image).hasDefects());
}

TEST(DefectDetectorTest, UnevenLightingIsNotADefect)
{
    // Soldan sağa 80 -> 220 aydınlatma gradyanı. Global bir eşik (ör. < 120)
    // sol kenarı tamamen kusur sayardı; yerel arka plan farkı saymaz.
    cv::Mat image(kHeight, kWidth, CV_8UC1);
    for (int x = 0; x < kWidth; ++x) {
        image.col(x).setTo(80 + 140 * x / (kWidth - 1));
    }
    addSpot(image, {160, 120}, 12, 20);

    const auto result = DefectDetector{}.detect(image);

    ASSERT_EQ(result.defects.size(), 1u);
    EXPECT_TRUE(result.defects[0].boundingBox.contains({160, 120}));
}

TEST(DefectDetectorTest, ThresholdControlsSensitivity)
{
    cv::Mat image = makeFabric();
    addSpot(image, {160, 120}, 12, kFabricGray - 50);  // hafif bir leke

    DetectorConfig sensitive;
    sensitive.threshold = 30;
    DetectorConfig strict;
    strict.threshold = 80;

    EXPECT_TRUE(DefectDetector(sensitive).detect(image).hasDefects());
    EXPECT_FALSE(DefectDetector(strict).detect(image).hasDefects());
}

TEST(DefectDetectorTest, AcceptsColorImages)
{
    cv::Mat image(kHeight, kWidth, CV_8UC3, cv::Scalar(kFabricGray, kFabricGray, kFabricGray));
    cv::circle(image, {100, 100}, 12, cv::Scalar(20, 20, 20), cv::FILLED);

    EXPECT_EQ(DefectDetector{}.detect(image).defects.size(), 1u);
}

// --- Hatalı girdi --------------------------------------------------------------

TEST(DefectDetectorTest, RejectsEmptyAndUnsupportedImages)
{
    const DefectDetector detector;

    EXPECT_THROW(detector.detect(cv::Mat()), std::invalid_argument);
    EXPECT_THROW(detector.detect(cv::Mat(10, 10, CV_32FC1)), std::invalid_argument);
}

TEST(DefectDetectorTest, RejectsInvalidConfigAndKeepsOldOne)
{
    DetectorConfig evenKernel;
    evenKernel.blurKernelSize = 4;
    EXPECT_THROW(DefectDetector{evenKernel}, std::invalid_argument);

    DefectDetector detector;
    DetectorConfig badThreshold;
    badThreshold.threshold = 300;
    EXPECT_THROW(detector.setConfig(badThreshold), std::invalid_argument);
    EXPECT_EQ(detector.config().threshold, DetectorConfig{}.threshold);
}

TEST(DefectDetectorTest, DrawDefectsMarksBoundingBoxInRed)
{
    cv::Mat image(kHeight, kWidth, CV_8UC3, cv::Scalar(kFabricGray, kFabricGray, kFabricGray));
    cv::circle(image, {100, 100}, 12, cv::Scalar(20, 20, 20), cv::FILLED);
    const auto result = DefectDetector{}.detect(image);
    ASSERT_TRUE(result.hasDefects());

    DefectDetector::drawDefects(image, result);

    const cv::Rect box = result.defects[0].boundingBox;
    EXPECT_EQ(image.at<cv::Vec3b>(box.y, box.x), cv::Vec3b(0, 0, 255));
}
