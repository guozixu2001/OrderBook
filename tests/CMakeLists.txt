# Tests CMakeLists.txt

# Find all test source files
file(GLOB_RECURSE TEST_SRC "*.cpp")

# Create test executable
add_executable(tests ${TEST_SRC})
set_property(TARGET tests PROPERTY CXX_STANDARD 17)
target_compile_options(tests PRIVATE -mtune=skylake -march=skylake -Wall -Wextra)
target_include_directories(tests PRIVATE
    ${CMAKE_SOURCE_DIR}/framework/include
    ${CMAKE_SOURCE_DIR}/impl/include
)

# Link with gtest and implementation library
target_link_libraries(tests PRIVATE
    gtest
    gtest_main
    impl_lib
)

# Add test to CTest
include(GoogleTest)
gtest_discover_tests(tests)