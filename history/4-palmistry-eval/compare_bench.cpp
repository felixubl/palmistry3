// compare_bench.cpp
// IMPORTANT: this file *includes* poker_eval.cpp so evaluate_u32 is in the same TU.
// Do NOT compile poker_eval.cpp separately when building this benchmark.

#define POKER_EVAL_LIB 1
#include "poker_eval.cpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <type_traits>
#include <vector>
#include <memory>
#include <boost/shared_ptr.hpp>   // handle older PokerStove builds returning boost::shared_ptr

// ---------- PokerStove (peval) ----------
#include <pokerstove/peval/Card.h>
#include <pokerstove/peval/CardSet.h>
#include <pokerstove/peval/Rank.h>
#include <pokerstove/peval/Suit.h>
#include <pokerstove/peval/HoldemHandEvaluator.h>

using pokerstove::Card;
using pokerstove::CardSet;
using pokerstove::Rank;
using pokerstove::Suit;
using pokerstove::HoldemHandEvaluator;

// Map (suit 0..3, rank 0..12 where 0=2,12=A) to PokerStove Card
static inline Card mkcard(int suit, int rank) {
    static const char R[13] = {'2','3','4','5','6','7','8','9','T','J','Q','K','A'};
    static const char S[4]  = {'c','d','h','s'};
    return Card(Rank(R[rank]), Suit(S[suit]));
}

// Convert our 4×13 bitmasks to a PokerStove CardSet
static inline CardSet to_cardset(const Hand& h) {
    CardSet cs;
    for (int s = 0; s < 4; ++s) {
        uint16_t m = h[s];
        while (m) {
#if defined(__GNUC__) || defined(__clang__)
            int r = __builtin_ctz(m);
#else
            int r = 0; uint16_t t = m; while ((t & 1u) == 0) { t >>= 1; ++r; }
#endif
            cs.insert(mkcard(s, r));
            m &= (m - 1);
        }
    }
    return cs;
}

// ------- timing helpers -------
struct Bench { double sec{}; double mhps{}; uint64_t checksum{}; };

static Bench bench_yours(const std::vector<Hand>& hands){
    using clock=std::chrono::high_resolution_clock;
    volatile uint64_t acc=0;
    auto t0=clock::now();
    for(const auto& h: hands) acc ^= evaluate_u32(h);
    auto t1=clock::now();
    double s=std::chrono::duration<double>(t1-t0).count();
    return {s, hands.size()/s/1e6, acc};
}

// ---- Helpers to "view" a possibly-pointer evaluation result ----
template<class T> static inline const T& view(const T& x) { return x; }
template<class T> static inline const T& view(const T* x) { return *x; }
template<class T> static inline const T& view(const std::unique_ptr<T>& x) { return *x; }
template<class T> static inline const T& view(const std::shared_ptr<T>& x) { return *x; }
template<class T> static inline const T& view(const boost::shared_ptr<T>& x) { return *x; }

// ---- Robust extractor for PokerStove return types ----
// We test many known shapes and report which one matched (once).
template<class T>
static inline uint64_t ps_value_any(const T& x, bool& reported) {
    auto report = [&](const char* path, uint64_t v){
        if (!reported) {
            std::cerr << "[PokerStove extractor] using " << path
                      << " ; sample value=" << v << "\n";
            reported = true;
        }
        return v;
    };

    const auto& y = view(x); // deref pointer/smart pointer if needed

    // Rank-like with value()
    if constexpr (requires { y.value(); }) {
        return report("y.value()", (uint64_t)y.value());
    }
    // Rank-like with getValue()
    else if constexpr (requires { y.getValue(); }) {
        return report("y.getValue()", (uint64_t)y.getValue());
    }
    // Structures exposing .rank()
    else if constexpr (requires { y.rank(); }) {
        const auto r = y.rank();
        if constexpr (requires { r.value(); }) return report("y.rank().value()", (uint64_t)r.value());
        else if constexpr (requires { r.getValue(); }) return report("y.rank().getValue()", (uint64_t)r.getValue());
    }
    // High/low evaluation shapes
    else if constexpr (requires { y.hi(); }) {
        const auto hi = y.hi();
        if constexpr (requires { hi.value(); }) return report("y.hi().value()", (uint64_t)hi.value());
        else if constexpr (requires { hi.getValue(); }) return report("y.hi().getValue()", (uint64_t)hi.getValue());
    }
    else if constexpr (requires { y.hiValue(); }) {
        return report("y.hiValue()", (uint64_t)y.hiValue());
    }
    // Some builds expose score()
    else if constexpr (requires { y.score(); }) {
        return report("y.score()", (uint64_t)y.score());
    }
    // Implicit convert-to-int
    else if constexpr (requires { (int)y; }) {
        return report("(int)y", (uint64_t)(int)y);
    }

    return report("fallback=1", 1ULL); // should not happen
}

static Bench bench_pokerstove(const std::vector<Hand>& hands){
    using clock = std::chrono::high_resolution_clock;
    HoldemHandEvaluator hev; // concrete 7-card Hold’em evaluator

    volatile uint64_t acc = 0;
    bool reported = false;   // print which accessor we used (once)
    auto t0 = clock::now();
    for (const auto& h : hands) {
        CardSet cs = to_cardset(h);
        auto ev = hev.evaluate(cs);
        acc ^= ps_value_any(ev, reported);
    }
    auto t1 = clock::now();

    double s = std::chrono::duration<double>(t1 - t0).count();
    return {s, hands.size()/s/1e6, acc};
}

int main(int argc, char** argv){
    size_t N = (argc>=2)? std::strtoull(argv[1],nullptr,10) : 10'000'000ULL;

    // 1) pre-generate identical random hands (not timed for eval)
    XorShift64 rng;                 // from poker_eval.cpp
    std::vector<Hand> hands; hands.reserve(N);
    auto tgen0=std::chrono::high_resolution_clock::now();
    for(size_t i=0;i<N;++i){
        hands.push_back(random_hand(rng));   // uses your existing generator
    }
    auto tgen1=std::chrono::high_resolution_clock::now();
    double gen_s = std::chrono::duration<double>(tgen1-tgen0).count();

    // tiny warmup
    volatile uint32_t w=0; for(size_t i=0;i<std::min<size_t>(N,10000);++i) w^=evaluate_u32(hands[i]);

    // 2) time each evaluator
    auto a = bench_yours(hands);
    auto b = bench_pokerstove(hands);

    std::cout<<std::fixed<<std::setprecision(3);
    std::cout<<"Hands generated: "<<N<<" in "<<gen_s<<" s ("<<N/gen_s/1e6<<" M/s gen)\n";
    std::cout<<"YOURS:      "<<N<<" in "<<a.sec<<" s ("<<a.mhps<<" M/s) checksum="<<a.checksum<<"\n";
    std::cout<<"PokerStove: "<<N<<" in "<<b.sec<<" s ("<<b.mhps<<" M/s) checksum="<<b.checksum<<"\n";
    return (int)w;
}
