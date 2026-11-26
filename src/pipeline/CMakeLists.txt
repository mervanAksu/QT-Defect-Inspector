# Thread'li işleme hattı: core + Qt Core/Gui (widget YOK).
# GUI uygulaması ve testler bu kütüphaneyi kullanır.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(inspector_pipeline STATIC
    # Q_OBJECT içeren başlıklar AUTOMOC'un görmesi için listelenir.
    include/inspector/CaptureThread.h
    include/inspector/ProcessingThread.h
    include/inspector/InspectionPipeline.h
    include/inspector/InspectionResult.h
    include/inspector/ImageConversion.h
    src/CaptureThread.cpp
    src/ProcessingThread.cpp
    src/InspectionPipeline.cpp
    src/ImageConversion.cpp)
add_library(inspector::pipeline ALIAS inspector_pipeline)

# AUTOMOC: Q_OBJECT gören her dosya için moc'u (Meta-Object Compiler) çalıştırır.
# moc, sinyal/slot ve runtime tip bilgisi için gereken C++ kodunu üretir.
set_target_properties(inspector_pipeline PROPERTIES AUTOMOC ON)

target_include_directories(inspector_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(inspector_pipeline PUBLIC inspector::core Qt6::Core Qt6::Gui)
inspector_set_warnings(inspector_pipeline)
