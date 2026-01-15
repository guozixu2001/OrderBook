#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>
#include <array>

// ============================================================================
// Cache Line Alignment Benchmarks
// ============================================================================

// Unaligned structure - two counters in same cache line
struct UnalignedCounters {
    uint64_t counter1;
    uint64_t counter2;
};

// Aligned structure - each counter in separate cache line
struct AlignedCounters {
    alignas(64) uint64_t counter1;
    alignas(64) uint64_t counter2;
};

// ============================================================================
// Benchmark: False Sharing - Unaligned (Multiple threads writing to adjacent bytes)
// ============================================================================
static void BM_FalseSharing_Unaligned(benchmark::State& state) {
    UnalignedCounters counters = {0, 0};
    int num_threads = state.range(0);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&counters, t]() {
                // Each thread updates one counter
                // But due to false sharing, they contend for the same cache line
                if (t % 2 == 0) {
                    for (volatile int i = 0; i < 10000; ++i) {
                        counters.counter1++;
                    }
                } else {
                    for (volatile int i = 0; i < 10000; ++i) {
                        counters.counter2++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }
}
BENCHMARK(BM_FalseSharing_Unaligned)->Range(2, 16)->UseRealTime();

// ============================================================================
// Benchmark: False Sharing - Aligned (Each counter in separate cache line)
// ============================================================================
static void BM_FalseSharing_Aligned(benchmark::State& state) {
    AlignedCounters counters = {0, 0};
    int num_threads = state.range(0);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&counters, t]() {
                // Each thread updates one counter
                // With alignment, no false sharing
                if (t % 2 == 0) {
                    for (volatile int i = 0; i < 10000; ++i) {
                        counters.counter1++;
                    }
                } else {
                    for (volatile int i = 0; i < 10000; ++i) {
                        counters.counter2++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }
}
BENCHMARK(BM_FalseSharing_Aligned)->Range(2, 16)->UseRealTime();

// ============================================================================
// Benchmark: Array Access Pattern - Contiguous (Good for cache)
// ============================================================================
static void BM_ArrayAccess_Contiguous(benchmark::State& state) {
    size_t size = state.range(0);
    std::vector<uint64_t> data(size);
    for (auto& v : data) v = 0;

    for (auto _ : state) {
        for (size_t i = 0; i < size; ++i) {
            data[i]++;
        }
    }
    benchmark::DoNotOptimize(data);
}
BENCHMARK(BM_ArrayAccess_Contiguous)->Range(64, 65536);

// ============================================================================
// Benchmark: Array Access Pattern - Strided (Bad for cache)
// ============================================================================
static void BM_ArrayAccess_Strided(benchmark::State& state) {
    size_t size = state.range(0);
    size_t stride = 64;  // Stride by cache line size
    std::vector<uint64_t> data(size * stride);
    for (auto& v : data) v = 0;

    for (auto _ : state) {
        for (size_t i = 0; i < size; ++i) {
            data[i * stride]++;
        }
    }
    benchmark::DoNotOptimize(data);
}
BENCHMARK(BM_ArrayAccess_Strided)->Range(64, 65536);

// ============================================================================
// Benchmark: Padding Impact - Structure with padding vs without
// ============================================================================
struct NoPadding {
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
};

struct WithPadding {
    uint64_t a;
    uint8_t pad1[56];   // Padding to next cache line
    uint64_t b;
    uint8_t pad2[56];
    uint64_t c;
    uint8_t pad3[56];
    uint64_t d;
};

static void BM_Structure_NoPadding(benchmark::State& state) {
    NoPadding obj = {0, 0, 0, 0};

    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i) {
            obj.a++;
            obj.b++;
            obj.c++;
            obj.d++;
        }
    }
    benchmark::DoNotOptimize(obj);
}
BENCHMARK(BM_Structure_NoPadding)->Range(1000, 100000);

static void BM_Structure_WithPadding(benchmark::State& state) {
    WithPadding obj = {0, {}, 0, {}, 0, {}, 0};

    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i) {
            obj.a++;
            obj.b++;
            obj.c++;
            obj.d++;
        }
    }
    benchmark::DoNotOptimize(obj);
}
BENCHMARK(BM_Structure_WithPadding)->Range(1000, 100000);

// ============================================================================
// Benchmark: Order Book Hash Map - Sequential Access Pattern
// ============================================================================
constexpr size_t ORDER_MAP_SIZE = 65536;
constexpr size_t PRICE_MAP_SIZE = 2048;

// Simulating order book hash map access pattern
static void BM_OrderMap_Sequential(benchmark::State& state) {
    alignas(64) std::array<uint64_t, ORDER_MAP_SIZE> order_map{};
    order_map.fill(0);

    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            size_t idx = i & (ORDER_MAP_SIZE - 1);
            order_map[idx]++;
        }
    }
    benchmark::DoNotOptimize(order_map);
}
BENCHMARK(BM_OrderMap_Sequential)->Range(1000, 100000);

// ============================================================================
// Benchmark: Order Map with Different Strides - Simulate collision pattern
// ============================================================================
static void BM_OrderMap_Collisions(benchmark::State& state) {
    alignas(64) std::array<uint64_t, ORDER_MAP_SIZE> order_map{};
    order_map.fill(0);

    for (auto _ : state) {
        for (size_t i = 0; i < state.range(0); ++i) {
            // Simulate many keys hashing to same bucket
            size_t idx = (i * 257) & (ORDER_MAP_SIZE - 1);  // 257 is prime
            order_map[idx]++;
        }
    }
    benchmark::DoNotOptimize(order_map);
}
BENCHMARK(BM_OrderMap_Collisions)->Range(1000, 100000);

// ============================================================================
// Benchmark: Multi-threaded Order Map Access
// ============================================================================
static void BM_OrderMap_MultiThreaded(benchmark::State& state) {
    alignas(64) std::array<uint64_t, ORDER_MAP_SIZE> order_map{};
    order_map.fill(0);
    int num_threads = state.range(0);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&order_map, t, num_threads]() {
                size_t start = (ORDER_MAP_SIZE / num_threads) * t;
                size_t end = (ORDER_MAP_SIZE / num_threads) * (t + 1);
                for (size_t iter = 0; iter < 1000; ++iter) {
                    for (size_t i = start; i < end; ++i) {
                        order_map[i]++;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }
    benchmark::DoNotOptimize(order_map);
}
BENCHMARK(BM_OrderMap_MultiThreaded)->Range(2, 16)->UseRealTime();

BENCHMARK_MAIN();
