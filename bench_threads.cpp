// Thread scaling, which is a separate question from the one bench.cpp answers.
//
// Deliberately harsher than bench.cpp: 16M hands is 128 MB, far too much to sit
// in cache, so this measures a real streaming workload rather than a warm one.
// Expect single-thread numbers a little below bench.cpp's for that reason.
//
// Two schedulers, because on a heterogeneous machine the choice is worth more
// than several rounds of instruction tuning. Splitting the work evenly hands an
// efficiency core the same share as a performance core and then waits for it, so
// the run finishes at efficiency-core speed and adding those cores makes things
// worse. Handing out chunks from an atomic cursor lets each core take what it can.
//
// The fold shape is what an equity simulator wants: hands in, one accumulated
// answer out, no per-hand score written to memory.

#include "evaluator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <pthread.h>
#include <random>
#include <thread>
#include <vector>

using namespace pokereval;
using Clock = std::chrono::steady_clock;

static constexpr size_t kHands = 16u << 20;
static constexpr int kPasses = 8;
static constexpr size_t kChunk = 1024;

static void pin_fast() { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }

static uint64_t fold_scalar(const Hand* in, size_t n) {
    const Evaluator eval;
    uint64_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += eval.evaluate(in[i]);
    return sum;
}

static uint64_t fold_batch(const Hand* in, size_t n) {
    const Evaluator eval;
    Score buf[kChunk];
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i += kChunk) {
        const size_t m = std::min(kChunk, n - i);
        eval.evaluate_batch(in + i, buf, m);
        for (size_t k = 0; k < m; ++k) sum += buf[k];
    }
    return sum;
}

using Fold = uint64_t (*)(const Hand*, size_t);

static double run_static(Fold fold, const std::vector<Hand>& h, unsigned T, uint64_t& ck) {
    std::vector<std::thread> pool;
    std::vector<uint64_t> sums(T, 0);
    const size_t per = (h.size() / T) & ~size_t(7);
    const auto t0 = Clock::now();
    for (unsigned t = 0; t < T; ++t) {
        const size_t lo = t * per;
        const size_t n = (t + 1 == T) ? h.size() - lo : per;
        pool.emplace_back([&, t, lo, n] {
            pin_fast();
            uint64_t s = 0;
            for (int p = 0; p < kPasses; ++p) s += fold(h.data() + lo, n);
            sums[t] = s;
        });
    }
    for (auto& th : pool) th.join();
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    ck = 0;
    for (uint64_t v : sums) ck += v;
    return secs;
}

// Grain divides kHands exactly, so no chunk ever straddles the end of the array
// and every scheduler does identical work. The checksum below is what proves it.
static double run_dynamic(Fold fold, const std::vector<Hand>& h, unsigned T, size_t grain,
                          uint64_t& ck) {
    const size_t total = h.size() * size_t(kPasses);
    std::atomic<size_t> cursor{0};
    std::vector<std::thread> pool;
    std::vector<uint64_t> sums(T, 0);
    const auto t0 = Clock::now();
    for (unsigned t = 0; t < T; ++t) {
        pool.emplace_back([&, t] {
            pin_fast();
            uint64_t s = 0;
            for (;;) {
                const size_t at = cursor.fetch_add(grain, std::memory_order_relaxed);
                if (at >= total) break;
                s += fold(h.data() + (at % h.size()), grain);
            }
            sums[t] = s;
        });
    }
    for (auto& th : pool) th.join();
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    ck = 0;
    for (uint64_t v : sums) ck += v;
    return secs;
}

int main() {
    std::mt19937_64 rng(20240727);
    Card deck[52];
    size_t n = 0;
    for (uint32_t s = 0; s < SuitCount; ++s) {
        for (uint32_t r = 0; r < RankCount; ++r) deck[n++] = make_card(s, r);
    }
    std::vector<Hand> hands(kHands);
    for (Hand& h : hands) {
        Hand v = 0;
        for (int i = 0; i < 7; ++i) {
            const int j = i + int(rng() % uint64_t(52 - i));
            std::swap(deck[i], deck[j]);
            add_card(v, deck[i]);
        }
        h = v;
    }

    const uint64_t work = uint64_t(kHands) * kPasses;
    const unsigned maxt = std::thread::hardware_concurrency();
    std::printf("%llu hands x %d passes = %llu evaluations per measurement, %u cores\n\n",
                (unsigned long long)kHands, kPasses, (unsigned long long)work, maxt);

    uint64_t ref = 0, other = 0;
    run_static(fold_batch, hands, 2, ref);
    run_dynamic(fold_batch, hands, 2, 65536, other);
    if (ref != other) {
        std::printf("*** schedulers disagree, timings meaningless ***\n");
        return 1;
    }
    std::printf("schedulers agree on 0x%llx\n", (unsigned long long)ref);

    struct Row { const char* name; Fold fold; };
    const Row rows[] = {{"scalar", fold_scalar}, {"batch", fold_batch}};
    for (const Row& row : rows) {
        std::printf("\n%-8s threads    static   dynamic   speedup vs 1 thread\n", row.name);
        double one = 0;
        for (unsigned t : {1u, 2u, 4u, 8u, 10u, 12u, maxt}) {
            if (t > maxt) continue;
            double bs = 1e9, bd = 1e9;
            uint64_t ck = 0;
            for (int r = 0; r < 3; ++r) {
                bs = std::min(bs, run_static(row.fold, hands, t, ck));
                bd = std::min(bd, run_dynamic(row.fold, hands, t, 65536, ck));
            }
            const double ms = double(work) / bs / 1e6, md = double(work) / bd / 1e6;
            if (t == 1) one = std::max(ms, md);
            std::printf("%-8s %7u   %7.0f   %7.0f   %5.2fx\n", "", t, ms, md,
                        std::max(ms, md) / one);
        }
    }
    std::printf("\nMhand/s. Quote the dynamic column: an even split cannot use the\n"
                "efficiency cores without being held back by them.\n");
    return 0;
}
