// bench.cpp
#include "evaluator.hpp"
#include "benchmarking.hpp"
#include <thread>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    uint64_t iters = 10'000'000;
    unsigned threads = 0;
    if (argc >= 2) {
        iters = std::strtoull(argv[1], nullptr, 10);
        if (iters == 0) iters = 1;
    }
    if (argc >= 3) {
        threads = (unsigned)std::strtoul(argv[2], nullptr, 10);
    }

    XorShift64 rng;
    std::vector<Hand> hands(iters);

    auto tgen0 = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < iters; ++i) hands[(size_t)i] = random_hand(rng);
    auto tgen1 = std::chrono::high_resolution_clock::now();

    // Cache warmup
    volatile uint32_t warm = 0;
    for (size_t i = 0; i < std::min<uint64_t>(iters, 10000); ++i) warm ^= evaluate_u32(hands[i]);

    auto seq = eval_sequential(hands);
    auto par = eval_parallel(hands, threads);

    double gen_s = std::chrono::duration<double>(tgen1 - tgen0).count();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Hands generated: " << iters << " in " << gen_s
              << " s  (" << (iters / gen_s / 1e6) << " M hands/s gen)\n";
    std::cout << "SEQ  eval:      " << iters << " in " << seq.seconds
              << " s  (" << seq.mhps << " M hands/s)  checksum=" << seq.checksum << "\n";
    std::cout << "PAR  eval (" << (threads ? threads : std::thread::hardware_concurrency()) << " threads): "
              << iters << " in " << par.seconds
              << " s  (" << par.mhps << " M hands/s)  checksum=" << par.checksum << "\n";
    return (warm == 0xFFFFFFFFu);
}
