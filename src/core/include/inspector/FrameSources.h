#pragma once

#include "inspector/FrameSource.h"

#include <opencv2/videoio.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace inspector {

/// Bir klasördeki .png/.jpg/.jpeg/.bmp dosyalarını ada göre sıralı okur.
/// Gerçek kamera yokken örnek kumaş fotoğraflarıyla çalışmak için.
class ImageFolderSource final : public FrameSource {
public:
    explicit ImageFolderSource(const std::filesystem::path& folder, bool loop = true,
                               double fps = 5.0);

    std::string description() const override { return m_folder.string(); }
    bool isLive() const noexcept override { return false; }
    double fps() const noexcept override { return m_fps; }

    std::size_t imageCount() const noexcept { return m_files.size(); }
    /// Okunamadığı için atlanan dosya sayısı (aynı dosya her turda tekrar sayılır).
    std::size_t skippedFiles() const noexcept { return m_skippedFiles; }

protected:
    bool readImage(cv::Mat& out) override;

private:
    std::filesystem::path m_folder;
    std::vector<std::filesystem::path> m_files;
    std::size_t m_position = 0;
    std::size_t m_skippedFiles = 0;
    bool m_loop;
    double m_fps;
};

/// Video dosyası (OpenCV'nin desteklediği her format: .mp4, .avi, ...).
class VideoFileSource final : public FrameSource {
public:
    explicit VideoFileSource(const std::filesystem::path& file, bool loop = true);

    std::string description() const override { return m_file.string(); }
    bool isLive() const noexcept override { return false; }
    double fps() const noexcept override { return m_fps; }

protected:
    bool readImage(cv::Mat& out) override;

private:
    std::filesystem::path m_file;
    cv::VideoCapture m_capture;
    bool m_loop;
    double m_fps = 0.0;
};

/// V4L2 üzerinden USB/dahili kamera (/dev/videoN).
class CameraSource final : public FrameSource {
public:
    explicit CameraSource(int deviceIndex);

    std::string description() const override;
    bool isLive() const noexcept override { return true; }
    double fps() const noexcept override { return m_fps; }
    bool canReopen() const noexcept override { return true; }
    bool reopen() override;

protected:
    bool readImage(cv::Mat& out) override;

private:
    int m_deviceIndex;
    cv::VideoCapture m_capture;
    double m_fps = 0.0;
};

}  // namespace inspector
