// Throughput on two workloads, through both entry points.
//
// The lexicographic C(52,7) walk is the standard number to quote, but
// consecutive hands in it differ by one card, which flatters the branch
// predictor. Randomly ordered hands are the realistic case for equity
// simulation, so both are reported.
//
// Each is run through the scalar evaluate and through the eight-wide
// evaluate_batch. A caller with hands in bulk gets the second number; a caller
// holding one hand gets the first. The enumeration cannot hand over an array it
// has not built yet, so it buffers, which is what a simulator generating hands
// on the fly would do.

#include "evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pokereval;

using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static void report(const char* what, double secs, uint64_t hands) {
    std::printf("%-30s %7.3f s  %7.1f Mhand/s  %5.2f ns/hand\n", what, secs,
                double(hands) / secs / 1e6, secs / double(hands) * 1e9);
}

static constexpr uint64_t kEnumerated = 133784560;
static constexpr size_t kBuffer = 1024;

static void build_deck(Hand bit[52]) {
    size_t n = 0;
    for (uint32_t suit = 0; suit < SuitCount; ++suit) {
        for (uint32_t rank = 0; rank < RankCount; ++rank) bit[n++] = card_bit(make_card(suit, rank));
    }
}

static double enumerate_scalar(uint64_t& sink) {
    Hand bit[52];
    build_deck(bit);
    const Evaluator eval;
    uint64_t sum = 0;
    const auto t0 = Clock::now();
    for (int a = 0; a < 52; ++a) { const Hand ha = bit[a];
    for (int b = a + 1; b < 52; ++b) { const Hand hb = ha | bit[b];
    for (int c = b + 1; c < 52; ++c) { const Hand hc = hb | bit[c];
    for (int d = c + 1; d < 52; ++d) { const Hand hd = hc | bit[d];
    for (int e = d + 1; e < 52; ++e) { const Hand he = hd | bit[e];
    for (int f = e + 1; f < 52; ++f) { const Hand hf = he | bit[f];
    for (int g = f + 1; g < 52; ++g) sum += eval.evaluate(hf | bit[g]);
    }}}}}}
    const double secs = since(t0);
    sink = sum;
    return secs;
}

static double enumerate_batch(uint64_t& sink) {
    Hand bit[52];
    build_deck(bit);
    const Evaluator eval;
    std::vector<Hand> buffer(kBuffer);
    std::vector<Score> scores(kBuffer);
    size_t held = 0;
    uint64_t sum = 0;
    const auto t0 = Clock::now();
    for (int a = 0; a < 52; ++a) { const Hand ha = bit[a];
    for (int b = a + 1; b < 52; ++b) { const Hand hb = ha | bit[b];
    for (int c = b + 1; c < 52; ++c) { const Hand hc = hb | bit[c];
    for (int d = c + 1; d < 52; ++d) { const Hand hd = hc | bit[d];
    for (int e = d + 1; e < 52; ++e) { const Hand he = hd | bit[e];
    for (int f = e + 1; f < 52; ++f) { const Hand hf = he | bit[f];
    for (int g = f + 1; g < 52; ++g) {
        buffer[held++] = hf | bit[g];
        if (held == kBuffer) {
            eval.evaluate_batch(buffer.data(), scores.data(), held);
            for (size_t i = 0; i < held; ++i) sum += scores[i];
            held = 0;
        }
    }}}}}}}
    eval.evaluate_batch(buffer.data(), scores.data(), held);
    for (size_t i = 0; i < held; ++i) sum += scores[i];
    const double secs = since(t0);
    sink = sum;
    return secs;
}

int main() {
    uint64_t enum_scalar_sum = 0, enum_batch_sum = 0;
    double best_scalar = 1e9, best_batch = 1e9;
    for (int r = 0; r < 3; ++r) {
        uint64_t s = 0;
        best_scalar = std::min(best_scalar, enumerate_scalar(s));
        enum_scalar_sum = s;
        best_batch = std::min(best_batch, enumerate_batch(s));
        enum_batch_sum = s;
    }
    report("C(52,7) enumeration", best_scalar, kEnumerated);
    report("C(52,7) enumeration, batch", best_batch, kEnumerated);

    // Random hands, pre-built so the timing covers evaluation only.
    std::mt19937_64 rng(20240727);
    Card deck[52];
    size_t n = 0;
    for (uint32_t suit = 0; suit < SuitCount; ++suit) {
        for (uint32_t rank = 0; rank < RankCount; ++rank) deck[n++] = make_card(suit, rank);
    }
    const size_t kRandom = 2u << 20;
    std::vector<Hand> hands(kRandom);
    for (Hand& h : hands) {
        Hand v = 0;
        for (int i = 0; i < 7; ++i) {
            const int j = i + int(rng() % uint64_t(52 - i));
            std::swap(deck[i], deck[j]);
            add_card(v, deck[i]);
        }
        h = v;
    }

    const Evaluator eval;
    std::vector<Score> scores(kRandom);
    uint64_t rand_scalar_sum = 0, rand_batch_sum = 0;
    best_scalar = 1e9;
    best_batch = 1e9;
    // All the scalar rounds, then all the batch rounds. Interleaving them looks
    // tidier and costs the scalar path a few percent, because the batch pass
    // streams 8 MB of scores through the cache between every pair of scalar
    // rounds and evicts the hands the scalar round is about to re-read.
    for (int r = 0; r < 9; ++r) {
        uint64_t sum = 0;
        const auto t0 = Clock::now();
        for (const Hand h : hands) sum += eval.evaluate(h);
        best_scalar = std::min(best_scalar, since(t0));
        rand_scalar_sum = sum;
    }
    for (int r = 0; r < 9; ++r) {
        const auto t0 = Clock::now();
        eval.evaluate_batch(hands.data(), scores.data(), kRandom);
        best_batch = std::min(best_batch, since(t0));
        uint64_t sum = 0;
        for (const Score s : scores) sum += s;
        rand_batch_sum = sum;
    }
    report("random hands", best_scalar, kRandom);
    report("random hands, batch", best_batch, kRandom);

    // Printed so the evaluated scores cannot be optimised away, and cross-checked
    // so a batch path that quietly disagrees cannot look like a speedup.
    const bool agree = enum_scalar_sum == enum_batch_sum && rand_scalar_sum == rand_batch_sum;
    std::printf("checksum 0x%llx  %s\n", (unsigned long long)(enum_scalar_sum ^ rand_scalar_sum),
                agree ? "scalar and batch agree" : "*** SCALAR AND BATCH DISAGREE ***");
    return agree ? 0 : 1;
}
