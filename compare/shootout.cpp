// palmistry3 against the main known 7-card evaluators.
//
// Rules of the comparison, so it can be argued with:
//   * One process, one set of hands, evaluators run alternately, best of N each.
//   * Each evaluator gets its OWN preferred prebuilt representation, built before
//     the clock starts. Nobody pays another's parsing cost.
//   * 128k hands, so every representation stays in cache and this measures
//     evaluation cost rather than memory bandwidth. Representation sizes differ
//     by 3.5x, which would otherwise dominate.
//   * Each representation is its OWN contiguous array. Packing them into one
//     struct would make every evaluator stride 72 bytes to read its own 8 to 28,
//     which penalises whoever has the most compact hand, i.e. us.
//   * Built with -flto. ACE and phevaluator live in separate translation units,
//     so without it they alone would pay a real call per hand while OMPEval and
//     palmistry3, both header-inline, would not.
//   * Agreement is checked, not assumed: category agreement hand by hand, and
//     pairwise ordering agreement over 4M random pairs. A fast wrong answer is
//     not a result.
#include "evaluator.hpp"
#include "omp/HandEvaluator.h"
extern "C" {
#include "ace_eval.h"
int evaluate_7cards(int a, int b, int c, int d, int e, int f, int g);
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace pokereval;
using Clock = std::chrono::steady_clock;

static constexpr size_t kHands = 128u << 10;
static constexpr int kPasses = 64;


int main() {
    // Canonical hands as (rank, suit) pairs, then translated once into each
    // evaluator's own encoding.
    std::mt19937_64 rng(20240727);
    std::vector<Hand> v_ours(kHands);
    std::vector<std::array<Card, ACEHAND>> v_ace(kHands);
    std::vector<omp::Hand> v_omp(kHands);
    std::vector<std::array<int, 7>> v_phe(kHands);
    int deck[52];
    for (int i = 0; i < 52; ++i) deck[i] = i;   // i = rank * 4 + suit

    const omp::HandEvaluator ompeval;

    for (size_t k = 0; k < kHands; ++k) {
        for (int i = 0; i < 7; ++i) std::swap(deck[i], deck[i + int(rng() % uint64_t(52 - i))]);
        Hand ours = 0;
        std::array<Card, ACEHAND> aceh{};
        omp::Hand omph = omp::Hand::empty();
        for (int i = 0; i < 7; ++i) {
            const int rank = deck[i] / 4, suit = deck[i] % 4;
            add_card(ours, make_card(uint32_t(suit), uint32_t(rank)));
            const Card ac = ACE_makecard(suit * 13 + rank);
            ACE_addcard(aceh.data(), ac);
            omph = omph + omp::Hand(unsigned(deck[i]));
            v_phe[k][i] = deck[i];
        }
        v_ours[k] = ours; v_ace[k] = aceh; v_omp[k] = omph;
    }

    // ---- agreement -------------------------------------------------------
    // ACE's categories match ours 0..7 but it numbers straight flush 9 and
    // leaves 8 unused, so one value is remapped.
    const Evaluator eval;
    std::vector<Score> mine(kHands);
    std::vector<uint32_t> aces(kHands), omps(kHands), phes(kHands);
    for (size_t k = 0; k < kHands; ++k) {
        mine[k] = eval.evaluate(v_ours[k]);
        aces[k] = ACE_evaluate(v_ace[k].data());
        omps[k] = ompeval.evaluate(v_omp[k]);
        phes[k] = uint32_t(evaluate_7cards(v_phe[k][0], v_phe[k][1], v_phe[k][2], v_phe[k][3],
                                          v_phe[k][4], v_phe[k][5], v_phe[k][6]));
    }
    size_t cat_bad = 0;
    for (size_t k = 0; k < kHands; ++k) {
        uint32_t a = ACE_rank(aces[k]);
        if (a == 9) a = 8;
        if (a != score_category(mine[k])) ++cat_bad;
    }
    std::printf("category agreement vs ACE      %s (%zu differ of %zu)\n",
                cat_bad ? "FAIL" : "ok", cat_bad, kHands);

    auto sgn = [](int64_t v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); };
    size_t bad_ace = 0, bad_omp = 0, bad_phe = 0;
    for (int t = 0; t < 4000000; ++t) {
        const size_t i = rng() % kHands, j = rng() % kHands;
        const int want = sgn(int64_t(mine[i]) - int64_t(mine[j]));
        if (sgn(int64_t(aces[i]) - int64_t(aces[j])) != want) ++bad_ace;
        if (sgn(int64_t(omps[i]) - int64_t(omps[j])) != want) ++bad_omp;
        // phevaluator ranks 1 as the strongest, so its order is reversed.
        if (sgn(int64_t(phes[j]) - int64_t(phes[i])) != want) ++bad_phe;
    }
    std::printf("ordering agreement, 4M pairs   ACE %s   OMPEval %s   phevaluator %s\n",
                bad_ace ? "FAIL" : "ok", bad_omp ? "FAIL" : "ok", bad_phe ? "FAIL" : "ok");
    if (cat_bad || bad_ace || bad_omp || bad_phe) {
        std::printf("  (ACE %zu, OMP %zu, phe %zu disagreements -- timings withheld)\n",
                    bad_ace, bad_omp, bad_phe);
        return 1;
    }

    // ---- speed -----------------------------------------------------------
    std::vector<Score> out(kHands);
    double b_ours = 1e9, b_batch = 1e9, b_ace = 1e9, b_omp = 1e9, b_phe = 1e9;
    volatile uint64_t sink = 0;
    for (int r = 0; r < 5; ++r) {
        uint64_t s;
        auto t0 = Clock::now();
        s = 0; for (int p = 0; p < kPasses; ++p) for (size_t k = 0; k < kHands; ++k) s += eval.evaluate(v_ours[k]);
        b_ours = std::min(b_ours, std::chrono::duration<double>(Clock::now() - t0).count()); sink += s;

        t0 = Clock::now();
        s = 0; for (int p = 0; p < kPasses; ++p) for (size_t k = 0; k < kHands; ++k) s += ACE_evaluate(v_ace[k].data());
        b_ace = std::min(b_ace, std::chrono::duration<double>(Clock::now() - t0).count()); sink += s;

        t0 = Clock::now();
        s = 0; for (int p = 0; p < kPasses; ++p) for (size_t k = 0; k < kHands; ++k) s += ompeval.evaluate(v_omp[k]);
        b_omp = std::min(b_omp, std::chrono::duration<double>(Clock::now() - t0).count()); sink += s;

        t0 = Clock::now();
        s = 0; for (int p = 0; p < kPasses; ++p) for (size_t k = 0; k < kHands; ++k)
            s += uint32_t(evaluate_7cards(v_phe[k][0], v_phe[k][1], v_phe[k][2], v_phe[k][3],
                                          v_phe[k][4], v_phe[k][5], v_phe[k][6]));
        b_phe = std::min(b_phe, std::chrono::duration<double>(Clock::now() - t0).count()); sink += s;
    }
    // The batch path needs its hands contiguous, so it gets its own array.
    const std::vector<Hand>& flat = v_ours;
    b_batch = 1e9;
    for (int r = 0; r < 5; ++r) {
        const auto t0 = Clock::now();
        for (int p = 0; p < kPasses; ++p) eval.evaluate_batch(flat.data(), out.data(), kHands);
        b_batch = std::min(b_batch, std::chrono::duration<double>(Clock::now() - t0).count());
        uint64_t s = 0; for (Score v : out) s += v; sink += s;
    }

    const double work = double(kHands) * kPasses;
    struct Row { const char* name; double secs; const char* table; };
    const Row rows[] = {
        {"palmistry3, evaluate_batch", b_batch, "0 B"},
        {"palmistry3, evaluate",       b_ours,  "0 B"},
        {"ACE_eval (table-free)",      b_ace,   "0 B"},
        {"OMPEval",                    b_omp,   "~190 KB"},
        {"phevaluator (HenryRLee)",    b_phe,   "~150 KB"},
    };
    std::printf("\n%-28s %10s %10s   %s\n", "evaluator", "Mhand/s", "ns/hand", "table");
    for (const Row& x : rows)
        std::printf("%-28s %10.0f %10.2f   %s\n", x.name, work / x.secs / 1e6,
                    x.secs / work * 1e9, x.table);
    return sink == 12345 ? 7 : 0;
}
