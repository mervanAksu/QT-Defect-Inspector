// Örnek kumaş görüntülerini üretir.
// Kullanım:  ./build/tools/generate_samples samples/fabric
//
// Her görüntü için labels.csv'ye "dosya,kusur_sayısı,kusurlar" satırı yazar.

#include "SampleGenerator.h"

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace inspector::samples;

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-folder>\n", argv[0]);
        return 2;
    }
    const fs::path outDir = argv[1];
    fs::create_directories(outDir);

    std::ofstream labels(outDir / "labels.csv");
    labels << "file,defect_count,defects\n";

    const auto specs = defaultSampleSet();
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto& spec = specs[i];
        const auto sample = generateFabric(spec.defects, static_cast<unsigned>(i + 1));

        char name[64];
        std::snprintf(name, sizeof(name), "%02zu_%s.jpg", i + 1, spec.name.c_str());
        const fs::path file = outDir / name;
        if (!cv::imwrite(file.string(), sample.image, {cv::IMWRITE_JPEG_QUALITY, 90})) {
            std::fprintf(stderr, "cannot write %s\n", file.c_str());
            return 1;
        }

        std::string kinds;
        for (const auto kind : spec.defects) {
            kinds += (kinds.empty() ? "" : "|") + toString(kind);
        }
        labels << name << ',' << spec.defects.size() << ',' << kinds << '\n';
        std::printf("%s  (%zu defects)\n", name, spec.defects.size());
    }
    return 0;
}
