// Throughput on two workloads.
//
// The lexicographic C(52,7) walk is the standard number to quote, but
// consecutive hands in it differ by one card, which flatters the branch
// predictor. Randomly ordered hands are the realistic case for equity
// simulation, so both are reported.

#include "evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pokereval;

static double enumerate_all(uint64_t& sink) {
    Card deck[52];
    size_t n = 0;
    for (uint32_t suit = 0; suit < SuitCount; ++suit) {
        for (uint32_t rank = 0; rank < RankCount; ++rank) deck[n++] = make_card(suit, rank);
    }
    Hand bit[52];
    for (size_t i = 0; i < 52; ++i) bit[i] = card_bit(deck[i]);

    const Evaluator eval;
    uint64_t sum = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int a = 0; a < 52; ++a) { const Hand ha = bit[a];
    for (int b = a + 1; b < 52; ++b) { const Hand hb = ha | bit[b];
    for (int c = b + 1; c < 52; ++c) { const Hand hc = hb | bit[c];
    for (int d = c + 1; d < 52; ++d) { const Hand hd = hc | bit[d];
    for (int e = d + 1; e < 52; ++e) { const Hand he = hd | bit[e];
    for (int f = e + 1; f < 52; ++f) { const Hand hf = he | bit[f];
    for (int g = f + 1; g < 52; ++g) sum += eval.evaluate(hf | bit[g]);
    }}}}}}
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sink = sum;
    return secs;
}

int main() {
    constexpr uint64_t kEnumerated = 133784560;
    uint64_t checksum = 0;

    double best = 1e9;
    for (int r = 0; r < 3; ++r) {
        uint64_t sum = 0;
        best = std::min(best, enumerate_all(sum));
        checksum ^= sum;
    }
    std::printf("C(52,7) enumeration  %7.3f s  %7.1f Mhand/s  %5.2f ns/hand\n",
                best, double(kEnumerated) / best / 1e6, best / double(kEnumerated) * 1e9);

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
    best = 1e9;
    for (int r = 0; r < 9; ++r) {
        uint64_t sum = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (const Hand h : hands) sum += eval.evaluate(h);
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        checksum ^= sum;
    }
    std::printf("random hands         %7.3f s  %7.1f Mhand/s  %5.2f ns/hand\n",
                best, double(kRandom) / best / 1e6, best / double(kRandom) * 1e9);

    // Printed so the evaluated scores cannot be optimised away.
    std::printf("checksum 0x%llx\n", (unsigned long long)checksum);
    return 0;
}
