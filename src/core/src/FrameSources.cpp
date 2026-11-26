#include "inspector/FrameSources.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio/registry.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace fs = std::filesystem;

namespace inspector {

// --- FrameSource (NVI) ---------------------------------------------------------

std::optional<Frame> FrameSource::next()
{
    cv::Mat image;
    if (!readImage(image) || image.empty()) {
        return std::nullopt;
    }
    Frame frame;
    frame.image = std::move(image);
    frame.index = m_nextIndex++;
    frame.timestamp = std::chrono::steady_clock::now();
    return frame;
}

// --- ImageFolderSource -----------------------------------------------------------

namespace {

bool hasImageExtension(const fs::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
}

}  // namespace

ImageFolderSource::ImageFolderSource(const fs::path& folder, bool loop, double fps)
    : m_folder(folder), m_loop(loop), m_fps(fps)
{
    if (!fs::is_directory(folder)) {
        throw std::runtime_error("Image folder not found: " + folder.string());
    }
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file() && hasImageExtension(entry.path())) {
            m_files.push_back(entry.path());
        }
    }
    if (m_files.empty()) {
        throw std::runtime_error("No images in folder: " + folder.string());
    }
    // directory_iterator sırası garanti değildir; tekrarlanabilirlik için sırala.
    std::sort(m_files.begin(), m_files.end());
}

bool ImageFolderSource::readImage(cv::Mat& out)
{
    if (m_position >= m_files.size()) {
        if (!m_loop) {
            return false;
        }
        m_position = 0;
    }
    out = cv::imread(m_files[m_position++].string(), cv::IMREAD_COLOR);
    return !out.empty();  // bozuk dosya -> akışı sonlandır
}

// --- VideoFileSource -------------------------------------------------------------

VideoFileSource::VideoFileSource(const fs::path& file, bool loop) : m_file(file), m_loop(loop)
{
    // Dosya yoksa OpenCV'ye hiç sormadan net bir hata ver. Aksi halde OpenCV açamadığı
    // dosya için TÜM arka uçları sırayla dener (GStreamer, gphoto2 kamera tarayıcısı...):
    // yavaş (~1 sn), konsola uyarı basar ve bu kütüphaneler bellek sızdırır (ASan'da görüldü).
    if (!fs::is_regular_file(file)) {
        throw std::runtime_error("Video file not found: " + file.string());
    }
    // FFmpeg varsa YALNIZCA onu kullan: öngörülebilir ve Ubuntu'nun OpenCV paketinde var.
    // FFmpeg açamıyorsa dosya bozuktur; diğer arka uçları denemek bir şey kazandırmaz,
    // sadece yukarıdaki yavaşlık ve sızıntıları geri getirir. FFmpeg'siz OpenCV
    // derlemelerinde otomatik seçime bırakıyoruz.
    const int api =
        cv::videoio_registry::hasBackend(cv::CAP_FFMPEG) ? cv::CAP_FFMPEG : cv::CAP_ANY;
    if (!m_capture.open(file.string(), api)) {
        throw std::runtime_error("Cannot open video file: " + file.string());
    }
    m_fps = m_capture.get(cv::CAP_PROP_FPS);
}

bool VideoFileSource::readImage(cv::Mat& out)
{
    if (m_capture.read(out)) {
        return true;
    }
    if (!m_loop) {
        return false;
    }
    m_capture.set(cv::CAP_PROP_POS_FRAMES, 0);  // başa sar
    return m_capture.read(out);
}

// --- CameraSource ----------------------------------------------------------------

CameraSource::CameraSource(int deviceIndex)
    : m_deviceIndex(deviceIndex), m_capture(deviceIndex, cv::CAP_V4L2)
{
    if (!m_capture.isOpened()) {
        throw std::runtime_error("Cannot open camera " + std::to_string(deviceIndex) +
                                 " (WSL2 has no USB camera access by default)");
    }
    m_fps = m_capture.get(cv::CAP_PROP_FPS);
}

std::string CameraSource::description() const
{
    return "camera:" + std::to_string(m_deviceIndex);
}

bool CameraSource::readImage(cv::Mat& out)
{
    return m_capture.read(out);
}

// --- Fabrika ---------------------------------------------------------------------

std::unique_ptr<FrameSource> createFrameSource(const std::string& spec, bool loop)
{
    if (spec.empty()) {
        throw std::invalid_argument("Empty frame source");
    }
    const bool isDeviceIndex = std::all_of(spec.begin(), spec.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
    if (isDeviceIndex) {
        return std::make_unique<CameraSource>(std::stoi(spec));
    }
    if (fs::is_directory(spec)) {
        return std::make_unique<ImageFolderSource>(spec, loop);
    }
    return std::make_unique<VideoFileSource>(spec, loop);
}

}  // namespace inspector
