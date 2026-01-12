# Benchmarks CMakeLists.txt

# Find benchmark source files
file(GLOB_RECURSE BENCH_SRC "*.cpp")

# Create benchmark executable (main benchmarks)
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

# ============================================================================
# Simple comparison benchmarks (RingBuffer vs Original SlidingWindowStats)
# ============================================================================

add_executable(bench_compare bench_sliding_window_simple.cpp)
set_property(TARGET bench_compare PROPERTY CXX_STANDARD 17)

target_compile_options(bench_compare PRIVATE -O3 -mtune=skylake -march=skylake -Wall -Wextra)

target_include_directories(bench_compare PRIVATE
    ${CMAKE_SOURCE_DIR}/framework/include
    ${CMAKE_SOURCE_DIR}/impl/include
)

target_link_libraries(bench_compare PRIVATE
    impl_lib
    benchmark::benchmark
    benchmark::benchmark_main
)

# Add benchmark to CTest (optional)
# include(GoogleTest)
# gtest_discover_tests(benchmarks)