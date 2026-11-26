# Qt Widgets arayüzü. Pencere sınıfları bir kütüphanede, böylece GUI testleri de kullanabilir.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(inspector_ui STATIC
    ImageView.h
    ImageView.cpp
    MainWindow.h
    MainWindow.cpp)
add_library(inspector::ui ALIAS inspector_ui)
set_target_properties(inspector_ui PROPERTIES AUTOMOC ON)
target_include_directories(inspector_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inspector_ui PUBLIC inspector::pipeline Qt6::Widgets)
inspector_set_warnings(inspector_ui)

add_executable(defect_inspector main.cpp)
target_link_libraries(defect_inspector PRIVATE inspector::ui)
target_compile_definitions(defect_inspector PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
    INSPECTOR_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples/fabric")
inspector_set_warnings(defect_inspector)
