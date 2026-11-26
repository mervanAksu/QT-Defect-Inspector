find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(core_tests
    test_thread_safe_queue.cpp
    test_defect_detector.cpp
    test_frame_sources.cpp)

target_link_libraries(core_tests PRIVATE inspector::core GTest::gtest_main)
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
