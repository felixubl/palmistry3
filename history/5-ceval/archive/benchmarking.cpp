// poker_bench.cpp
#include "benchmarking.hpp"
#include <chrono>
#include <thread>
#include <algorithm>

BenchResult eval_sequential(const std::vector<Hand>& hands) {
    using clock = std::chrono::high_resolution_clock;
    volatile uint32_t sink = 0;
    auto t0 = clock::now();
    for (const auto& h : hands) sink ^= evaluate_u32(h);
    auto t1 = clock::now();
    double s = std::chrono::duration<double>(t1 - t0).count();
    double mhps = hands.empty() ? 0.0 : (hands.size() / s) / 1e6;
    return {s, mhps, (uint32_t)sink};
}

BenchResult eval_parallel(const std::vector<Hand>& hands, unsigned threads) {
    using clock = std::chrono::high_resolution_clock;
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    threads = std::max(1u, std::min<unsigned>(threads, (unsigned)hands.size()));

    std::vector<uint32_t> local(threads, 0);
    std::vector<std::thread> pool;
    pool.reserve(threads);

    auto t0 = clock::now();

    const size_t n = hands.size();
    const size_t chunk = (n + threads - 1) / threads;

    for (unsigned t = 0; t < threads; ++t) {
        size_t begin = t * chunk;
        if (begin >= n) break;
        size_t end = std::min(n, begin + chunk);
        pool.emplace_back([&, t, begin, end]{
            uint32_t acc = 0;
            size_t i = begin;
            for (; i + 3 < end; i += 4) {
                acc ^= evaluate_u32(hands[i+0]);
                acc ^= evaluate_u32(hands[i+1]);
                acc ^= evaluate_u32(hands[i+2]);
                acc ^= evaluate_u32(hands[i+3]);
            }
            for (; i < end; ++i) acc ^= evaluate_u32(hands[i]);
            local[t] = acc;
        });
    }
    for (auto& th : pool) th.join();

    auto t1 = clock::now();
    uint32_t checksum = 0;
    for (uint32_t x : local) checksum ^= x;

    double s = std::chrono::duration<double>(t1 - t0).count();
    double mhps = hands.empty() ? 0.0 : (hands.size() / s) / 1e6;
    return {s, mhps, checksum};
}
