cmake_minimum_required(VERSION 3.22)

project(DefectInspector
    VERSION 0.1.0
    DESCRIPTION "Qt/OpenCV camera monitoring and defect detection"
    LANGUAGES CXX)

# --- Dil standardı ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++17 (gnu++17 değil)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # clangd / VS Code IntelliSense için

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- Seçenekler ---------------------------------------------------------------
option(INSPECTOR_BUILD_TESTS "Build GoogleTest unit tests" ON)
set(INSPECTOR_SANITIZER "" CACHE STRING "Sanitizer to enable: '', address, thread")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

if(INSPECTOR_SANITIZER)
    message(STATUS "Sanitizer enabled: ${INSPECTOR_SANITIZER}")
    add_compile_options(-fsanitize=${INSPECTOR_SANITIZER} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${INSPECTOR_SANITIZER})
endif()

# --- Alt projeler -------------------------------------------------------------
add_subdirectory(src/core)       # Qt'den bağımsız çekirdek (kuyruk, dedektör, kaynaklar)
add_subdirectory(src/pipeline)   # Capture/processing thread'leri (Qt Core)
add_subdirectory(src/net)        # TCP/UDP sonuç yayını (JSON Lines)
add_subdirectory(tools)          # Örnek görüntü üreteci
add_subdirectory(src/app)        # Qt Widgets uygulaması: defect_inspector
# add_subdirectory(src/receiver) # Sonuçları alan istemci     (Gün 8)

if(INSPECTOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
