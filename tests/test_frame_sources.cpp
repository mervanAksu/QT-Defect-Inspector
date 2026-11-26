#include "inspector/FrameSources.h"

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace inspector;

namespace {

/// Her test için geçici bir klasör açıp test bitince silen fixture.
class FrameSourceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / ("inspector_test_" + std::string(info->name()));
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    /// Pikselleri `gray` değerinde küçük bir görüntü yazar.
    void writeImage(const std::string& name, int gray) const
    {
        const cv::Mat image(48, 64, CV_8UC3, cv::Scalar(gray, gray, gray));
        ASSERT_TRUE(cv::imwrite((m_dir / name).string(), image));
    }

    /// `frameCount` karelik MJPG .avi yazar. Codec yoksa boş yol döner.
    fs::path writeVideo(int frameCount) const
    {
        const fs::path file = m_dir / "clip.avi";
        cv::VideoWriter writer(file.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 10.0,
                               cv::Size(64, 48));
        if (!writer.isOpened()) {
            return {};
        }
        for (int i = 0; i < frameCount; ++i) {
            writer.write(cv::Mat(48, 64, CV_8UC3, cv::Scalar(i * 40, i * 40, i * 40)));
        }
        return file;
    }

    fs::path m_dir;
};

int grayAt00(const Frame& frame)
{
    return frame.image.at<cv::Vec3b>(0, 0)[0];
}

}  // namespace

// --- ImageFolderSource -----------------------------------------------------------

TEST_F(FrameSourceTest, FolderReadsImagesInNameOrderAndSkipsOtherFiles)
{
    writeImage("b.png", 100);
    writeImage("a.png", 50);
    writeImage("c.JPG", 150);  // büyük harfli uzantı da kabul edilmeli
    std::ofstream(m_dir / "notes.txt") << "not an image";

    ImageFolderSource source(m_dir, /*loop=*/false);
    ASSERT_EQ(source.imageCount(), 3u);

    const auto f0 = source.next();
    const auto f1 = source.next();
    const auto f2 = source.next();
    ASSERT_TRUE(f0 && f1 && f2);
    EXPECT_EQ(grayAt00(*f0), 50);   // a.png
    EXPECT_EQ(grayAt00(*f1), 100);  // b.png
    EXPECT_NEAR(grayAt00(*f2), 150, 2);  // c.JPG (JPEG kayıplı)
    EXPECT_EQ(source.next(), std::nullopt);  // loop kapalı: akış biter
}

TEST_F(FrameSourceTest, FolderLoopsAndKeepsIncreasingFrameIndex)
{
    writeImage("only.png", 80);
    ImageFolderSource source(m_dir, /*loop=*/true);

    for (std::uint64_t expected = 0; expected < 5; ++expected) {
        const auto frame = source.next();
        ASSERT_TRUE(frame);
        EXPECT_EQ(frame->index, expected);
    }
}

TEST_F(FrameSourceTest, FolderThrowsWhenMissingOrEmpty)
{
    EXPECT_THROW(ImageFolderSource(m_dir / "does_not_exist"), std::runtime_error);
    EXPECT_THROW(ImageFolderSource{m_dir}, std::runtime_error);  // klasör var ama boş
}

TEST_F(FrameSourceTest, FolderSkipsUnreadableImages)
{
    writeImage("a.png", 50);
    std::ofstream(m_dir / "b.png") << "half-copied file, not a PNG";
    writeImage("c.png", 150);

    ImageFolderSource source(m_dir, /*loop=*/false);
    const auto first = source.next();
    const auto second = source.next();  // b.png atlanır, c.png gelir
    ASSERT_TRUE(first && second);
    EXPECT_EQ(grayAt00(*first), 50);
    EXPECT_EQ(grayAt00(*second), 150);
    EXPECT_EQ(second->index, 1u);  // sıra numarasında boşluk yok
    EXPECT_EQ(source.next(), std::nullopt);
    EXPECT_EQ(source.skippedFiles(), 1u);
}

TEST_F(FrameSourceTest, FolderOfOnlyUnreadableImagesEndsInsteadOfLooping)
{
    std::ofstream(m_dir / "x.png") << "broken";
    std::ofstream(m_dir / "y.jpg") << "broken";

    ImageFolderSource source(m_dir, /*loop=*/true);  // loop açıkken bile sonsuz döngü olmamalı
    EXPECT_EQ(source.next(), std::nullopt);
    EXPECT_EQ(source.skippedFiles(), 2u);
}

TEST_F(FrameSourceTest, FolderIsNotLiveAndReportsFps)
{
    writeImage("x.png", 10);
    const ImageFolderSource source(m_dir, true, 12.5);

    EXPECT_FALSE(source.isLive());
    EXPECT_DOUBLE_EQ(source.fps(), 12.5);
}

// --- VideoFileSource -------------------------------------------------------------

TEST_F(FrameSourceTest, VideoReadsAllFramesThenEnds)
{
    const fs::path file = writeVideo(5);
    if (file.empty()) {
        GTEST_SKIP() << "MJPG writer not available";
    }

    VideoFileSource source(file, /*loop=*/false);
    EXPECT_NEAR(source.fps(), 10.0, 0.01);

    int count = 0;
    while (source.next()) {
        ++count;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(FrameSourceTest, VideoLoopsBackToStart)
{
    const fs::path file = writeVideo(3);
    if (file.empty()) {
        GTEST_SKIP() << "MJPG writer not available";
    }

    VideoFileSource source(file, /*loop=*/true);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(source.next());
    }
    const auto wrapped = source.next();  // 4. kare = videonun ilk karesi
    ASSERT_TRUE(wrapped);
    EXPECT_NEAR(grayAt00(*wrapped), 0, 3);
    EXPECT_EQ(wrapped->index, 3u);  // sıra numarası sıfırlanmaz
}

TEST_F(FrameSourceTest, VideoThrowsForMissingFile)
{
    // Dosya yoksa OpenCV arka uçlarını denemeden, açık bir mesajla hemen başarısız olmalı.
    try {
        VideoFileSource source(m_dir / "missing.mp4");
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("not found"), std::string::npos) << e.what();
    }
}

TEST_F(FrameSourceTest, VideoThrowsForCorruptFile)
{
    std::ofstream(m_dir / "corrupt.mp4") << "this is not a video";
    EXPECT_THROW(VideoFileSource(m_dir / "corrupt.mp4"), std::runtime_error);
}

// --- Fabrika ---------------------------------------------------------------------

TEST_F(FrameSourceTest, FactoryPicksFolderOrVideoBySpec)
{
    writeImage("a.png", 1);
    const auto folder = createFrameSource(m_dir.string());
    EXPECT_NE(dynamic_cast<ImageFolderSource*>(folder.get()), nullptr);

    const fs::path file = writeVideo(2);
    if (!file.empty()) {
        const auto video = createFrameSource(file.string());
        EXPECT_NE(dynamic_cast<VideoFileSource*>(video.get()), nullptr);
    }
}

TEST_F(FrameSourceTest, FactoryRejectsEmptySpecAndMissingCamera)
{
    EXPECT_THROW(createFrameSource(""), std::invalid_argument);
    EXPECT_THROW(createFrameSource("97"), std::runtime_error);  // /dev/video97 yok
}
