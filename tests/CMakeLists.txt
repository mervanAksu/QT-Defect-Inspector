find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(core_tests
    test_thread_safe_queue.cpp
    test_defect_detector.cpp
    test_frame_sources.cpp
    test_sample_images.cpp
    test_rate_meter.cpp)

target_link_libraries(core_tests PRIVATE inspector::core inspector::samplegen GTest::gtest_main)
inspector_set_warnings(core_tests)

# Her TEST()'i ctest'e ayrı bir test olarak kaydeder.
gtest_discover_tests(core_tests)

# --- Qt Test: thread'li pipeline entegrasyon testleri -------------------------
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(pipeline_tests test_inspection_pipeline.cpp)
set_target_properties(pipeline_tests PROPERTIES AUTOMOC ON)
target_link_libraries(pipeline_tests PRIVATE inspector::pipeline Qt6::Test)
inspector_set_warnings(pipeline_tests)

add_test(NAME pipeline_tests COMMAND pipeline_tests)
set_tests_properties(pipeline_tests PROPERTIES TIMEOUT 60)

# --- Qt Test: ağ katmanı (protokol, çerçeveleme, localhost TCP/UDP) -----------
add_executable(net_tests test_network.cpp)
set_target_properties(net_tests PROPERTIES AUTOMOC ON)
target_link_libraries(net_tests PRIVATE inspector::net Qt6::Test)
inspector_set_warnings(net_tests)

add_test(NAME net_tests COMMAND net_tests)
set_tests_properties(net_tests PROPERTIES TIMEOUT 60)

# --- Qt Test: GUI testleri (ekransız "offscreen" platformda) ------------------
add_executable(ui_tests test_main_window.cpp)
set_target_properties(ui_tests PROPERTIES AUTOMOC ON)
target_link_libraries(ui_tests PRIVATE inspector::ui inspector::samplegen Qt6::Test)
inspector_set_warnings(ui_tests)

add_test(NAME ui_tests COMMAND ui_tests)
set_tests_properties(ui_tests PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
