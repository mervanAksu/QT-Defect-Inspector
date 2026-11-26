find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(core_tests
    test_thread_safe_queue.cpp)

target_link_libraries(core_tests PRIVATE inspector::core GTest::gtest_main)
inspector_set_warnings(core_tests)

# Her TEST()'i ctest'e ayrı bir test olarak kaydeder.
gtest_discover_tests(core_tests)
