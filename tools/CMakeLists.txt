# Geliştirme araçları: sentetik örnek kumaş görüntüsü üreteci.
add_library(inspector_samplegen STATIC SampleGenerator.cpp)
add_library(inspector::samplegen ALIAS inspector_samplegen)
target_include_directories(inspector_samplegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(inspector_samplegen PUBLIC inspector::core)
inspector_set_warnings(inspector_samplegen)

add_executable(generate_samples generate_samples.cpp)
target_link_libraries(generate_samples PRIVATE inspector::samplegen)
inspector_set_warnings(generate_samples)
