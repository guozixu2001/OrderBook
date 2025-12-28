# Benchmarks CMakeLists.txt

# Find benchmark source files
file(GLOB_RECURSE BENCH_SRC "*.cpp")

# Create benchmark executable
add_executable(benchmarks ${BENCH_SRC})
set_property(TARGET benchmarks PROPERTY CXX_STANDARD 17)

# Optimize for benchmarking: highest optimization level
target_compile_options(benchmarks PRIVATE -O3 -mtune=skylake -march=skylake -Wall -Wextra)

# Include directories
target_include_directories(benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/framework/include
    ${CMAKE_SOURCE_DIR}/impl/include
)

# Link with implementation library and benchmark library
target_link_libraries(benchmarks PRIVATE
    impl_lib
    benchmark::benchmark
    benchmark::benchmark_main
)

# Add benchmark to CTest (optional)
# include(GoogleTest)
# gtest_discover_tests(benchmarks)