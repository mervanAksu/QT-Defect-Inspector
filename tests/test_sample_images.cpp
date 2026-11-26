// Uçtan uca gerçekçilik testi: dokulu, düzensiz aydınlatılmış, gürültülü sentetik
// kumaşta dedektör, üretecin yerleştirdiği kusurları (ve yalnızca onları) bulmalı.

#include "SampleGenerator.h"
#include "inspector/DefectDetector.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace inspector;
using namespace inspector::samples;

namespace {

bool overlaps(const cv::Rect& a, const cv::Rect& b)
{
    return (a & b).area() > 0;
}

}  // namespace

class SampleImagesTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(SampleImagesTest, DetectorFindsExactlyTheGeneratedDefects)
{
    const std::size_t i = GetParam();
    const SampleSpec spec = defaultSampleSet().at(i);
    const GeneratedSample sample = generateFabric(spec.defects, static_cast<unsigned>(i + 1));

    const DetectionResult result = DefectDetector{}.detect(sample.image);

    ASSERT_EQ(result.defects.size(), spec.defects.size()) << "sample " << i + 1 << " " << spec.name;
    // Her gerçek kusur, bulunan bir kutuyla örtüşmeli.
    for (const cv::Rect& truth : sample.defectBoxes) {
        const bool found = std::any_of(result.defects.begin(), result.defects.end(),
                                       [&](const Defect& d) { return overlaps(d.boundingBox, truth); });
        EXPECT_TRUE(found) << "missed defect at " << truth;
    }
}

INSTANTIATE_TEST_SUITE_P(DefaultSet, SampleImagesTest,
                         ::testing::Range<std::size_t>(0, defaultSampleSet().size()));

TEST(SampleGeneratorTest, IsDeterministicForTheSameSeed)
{
    const auto a = generateFabric({DefectKind::Stain}, 42);
    const auto b = generateFabric({DefectKind::Stain}, 42);
    EXPECT_EQ(cv::norm(a.image, b.image, cv::NORM_INF), 0.0);
}

TEST(SampleGeneratorTest, RejectsMoreDefectsThanGridCells)
{
    const std::vector<DefectKind> seven(7, DefectKind::Hole);
    EXPECT_THROW(generateFabric(seven, 1), std::invalid_argument);
}
