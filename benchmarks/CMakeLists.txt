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

# Loop boundary check optimization benchmark
add_executable(bench_loop_optimization bench_loop_optimization.cpp)
set_property(TARGET bench_loop_optimization PROPERTY CXX_STANDARD 17)

target_compile_options(bench_loop_optimization PRIVATE -O3 -mtune=skylake -march=skylake -Wall -Wextra)

target_include_directories(bench_loop_optimization PRIVATE
    ${CMAKE_SOURCE_DIR}/framework/include
    ${CMAKE_SOURCE_DIR}/impl/include
)

target_link_libraries(bench_loop_optimization PRIVATE
    impl_lib
    benchmark::benchmark
    benchmark::benchmark_main
)

# Prefetch optimization benchmark
add_executable(bench_prefetch bench_prefetch.cpp)
set_property(TARGET bench_prefetch PROPERTY CXX_STANDARD 17)

target_compile_options(bench_prefetch PRIVATE -O3 -mtune=skylake -march=skylake -Wall -Wextra)

target_include_directories(bench_prefetch PRIVATE
    ${CMAKE_SOURCE_DIR}/framework/include
    ${CMAKE_SOURCE_DIR}/impl/include
)

target_link_libraries(bench_prefetch PRIVATE
    impl_lib
    benchmark::benchmark
    benchmark::benchmark_main
)

# Prefetch comparison benchmark (with vs without)
add_executable(bench_prefetch_compare bench_prefetch_compare.cpp)
set_property(TARGET bench_prefetch_compare PROPERTY CXX_STANDARD 17)

target_compile_options(bench_prefetch_compare PRIVATE -O3 -mtune=skylake -march=skylake -Wall -Wextra)

target_include_directories(bench_prefetch_compare PRIVATE
    ${CMAKE_SOURCE_DIR}/framework/include
    ${CMAKE_SOURCE_DIR}/impl/include
)

target_link_libraries(bench_prefetch_compare PRIVATE
    impl_lib
    benchmark::benchmark
    benchmark::benchmark_main
)

# Add benchmark to CTest (optional)
# include(GoogleTest)
# gtest_discover_tests(benchmarks)
