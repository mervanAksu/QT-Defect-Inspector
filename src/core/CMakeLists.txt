# Çekirdek kütüphane: Qt'ye bağımlı DEĞİL, sadece C++17 (+ ileride OpenCV).
# Şimdilik yalnızca başlık dosyası (header-only) içerdiği için INTERFACE.
# Gün 3'te DefectDetector.cpp eklenince STATIC kütüphaneye dönüşecek.
find_package(Threads REQUIRED)

add_library(inspector_core INTERFACE)
add_library(inspector::core ALIAS inspector_core)

target_include_directories(inspector_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(inspector_core INTERFACE Threads::Threads)
