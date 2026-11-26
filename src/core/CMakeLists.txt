# Çekirdek kütüphane: Qt'ye bağımlı DEĞİL; yalnızca C++17 + OpenCV.
# Böylece GUI olmadan test edilebilir ve başka uygulamalarda yeniden kullanılabilir.
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)

add_library(inspector_core STATIC
    src/DefectDetector.cpp)
add_library(inspector::core ALIAS inspector_core)

# PUBLIC: bu kütüphaneyi kullanan hedefler (testler, uygulama) de bu başlıkları
# ve OpenCV'yi otomatik olarak görür.
target_include_directories(inspector_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(inspector_core SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})

target_link_libraries(inspector_core PUBLIC Threads::Threads ${OpenCV_LIBS})
inspector_set_warnings(inspector_core)
