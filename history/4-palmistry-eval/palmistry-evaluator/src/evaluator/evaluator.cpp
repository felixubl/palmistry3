// poker_eval.cpp - Clean core evaluator only
// g++ -O3 -march=native -std=c++20 poker_eval.cpp -o poker_eval
#include <array>
#include <cstdint>

// ---------- Types & constants ----------
using Hand = std::array<uint16_t, 4>;

static constexpr uint16_t MASK13     = (1u << 13) - 1u;
static constexpr uint16_t WHEEL_MASK = (1u << 12) | (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);

// RUN5[i] = 5 consecutive bits, window start i in [0,8]
static constexpr std::array<uint16_t, 9> make_RUN5() {
    std::array<uint16_t, 9> a{};
    for (int i = 0; i < 9; ++i) {
        a[i] = (uint16_t)((((1u << 5) - 1u) << i) & MASK13);
    }
    return a;
}
static constexpr auto RUN5 = make_RUN5();

// ---------- LUTs (8192 entries for 13-bit masks) ----------
struct LUTs {
    std::array<uint8_t, 8192> popcnt13{};
    std::array<int8_t, 8192>  hibit13{};
    std::array<int16_t, 8192> straight_end13{};

    constexpr LUTs() {
        // popcount
        for (size_t m = 0; m < popcnt13.size(); ++m) {
            uint16_t v = (uint16_t)m;
            // simple popcount (13-bit), constexpr loop
            uint8_t c = 0;
            while (v) { v &= (uint16_t)(v - 1); ++c; }
            popcnt13[m] = c;
        }

        // msb index (aka floor(log2(m))), -1 for 0
        hibit13[0] = -1;
        for (size_t m = 1; m < hibit13.size(); ++m) {
            uint16_t v = (uint16_t)m;
            // 16-bit: index = 15 - clz16(v)
            int clz = 0;
            uint16_t t = v;
            while ((t & 0x8000u) == 0) { t <<= 1; ++clz; }
            hibit13[m] = (int8_t)(15 - clz);
        }

        // straight end
        for (size_t m = 0; m < straight_end13.size(); ++m) {
            int16_t end = -1;
            uint16_t mm = (uint16_t)m;
            for (int s = 8; s >= 0; --s) {
                uint16_t w = RUN5[(size_t)s];
                if ( (mm & w) == w ) { end = (int16_t)(s + 4); break; }
            }
            if (end < 3 && ((mm & WHEEL_MASK) == WHEEL_MASK)) end = 3; // A-2-3-4-5
            straight_end13[m] = end;
        }
    }
};
static constexpr LUTs LUT;

// ---------- Core evaluator helpers ----------
static inline int popcnt13(uint16_t mask) { return (int)LUT.popcnt13[(size_t)mask]; }
static inline int straight_end_from_mask(uint16_t mask) { return (int)LUT.straight_end13[(size_t)mask]; }
static inline int msb_index(uint16_t mask) { return (int)LUT.hibit13[(size_t)mask]; }

// Return the top five ranks (hi..lo) present in mask; fill with -1 if fewer.
static inline std::array<int,5> top5_from_mask(uint16_t m) {
    std::array<int,5> out{ -1,-1,-1,-1,-1 };
    for (int i = 0; i < 5; ++i) {
        int r = msb_index(m);
        out[(size_t)i] = r;
        if (r < 0) break;
        m = (uint16_t)(m & ~(uint16_t)(1u << (uint16_t)r));
    }
    return out;
}

// Pack cat + 5 tiebreakers into u32 (higher is better).
static inline uint32_t pack_score(int cat, int r0, int r1, int r2, int r3, int r4) {
    return ((uint32_t)cat << 20)
         | ((uint32_t)r0  << 16)
         | ((uint32_t)r1  << 12)
         | ((uint32_t)r2  <<  8)
         | ((uint32_t)r3  <<  4)
         |  (uint32_t)r4;
}


// ---------- Core evaluator ----------
// Evaluate 7 cards in bitboard form and return a packed u32 strength.
inline uint32_t evaluate_u32(const Hand& hand) {
    uint16_t h0 = hand[0], h1 = hand[1], h2 = hand[2], h3 = hand[3];

    uint16_t ranks = (uint16_t)((h0 | h1 | h2 | h3) & MASK13);
    uint16_t ge4   = (uint16_t)((h0 & h1 & h2 & h3) & MASK13);
    uint16_t ge2   = (uint16_t)(((h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)) & MASK13);
    uint16_t ge3   = (uint16_t)(((h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)) & MASK13);

    // Straight flush
    int best_sf = -1;
    if (popcnt13(h0) >= 5) { int se = straight_end_from_mask(h0); if (se > best_sf) best_sf = se; }
    if (popcnt13(h1) >= 5) { int se = straight_end_from_mask(h1); if (se > best_sf) best_sf = se; }
    if (popcnt13(h2) >= 5) { int se = straight_end_from_mask(h2); if (se > best_sf) best_sf = se; }
    if (popcnt13(h3) >= 5) { int se = straight_end_from_mask(h3); if (se > best_sf) best_sf = se; }
    if (best_sf >= 0) return pack_score(8, best_sf, 0, 0, 0, 0);

    // Four of a kind
    uint16_t all4 = ge4;
    if (all4) {
        int qr = msb_index(all4);
        uint16_t kmask = (uint16_t)((ranks & ~(uint16_t)(1u << (uint16_t)qr)) & MASK13);
        int kr = msb_index(kmask);
        return pack_score(7, qr, kr, 0, 0, 0);
    }

    // Full house
    uint16_t exactly3 = (uint16_t)((ge3 & ~ge4) & MASK13);
    if (exactly3) {
        int tr1 = msb_index(exactly3);
        uint16_t pairs_only = (uint16_t)((ge2 & ~ge3) & MASK13);
        uint16_t pmask = (uint16_t)((pairs_only & ~(uint16_t)(1u << (uint16_t)tr1)) & MASK13);
        int pr = msb_index(pmask);
        if (pr >= 0) return pack_score(6, tr1, pr, 0, 0, 0);

        uint16_t tr2mask = (uint16_t)((exactly3 & ~(uint16_t)(1u << (uint16_t)tr1)) & MASK13);
        int tr2 = msb_index(tr2mask);
        if (tr2 >= 0) return pack_score(6, tr1, tr2, 0, 0, 0);
    }

    // Flush (top-5 in-suit)
    uint16_t m = 0;
    if (popcnt13(h0) >= 5) m = h0;
    else if (popcnt13(h1) >= 5) m = h1;
    else if (popcnt13(h2) >= 5) m = h2;
    else if (popcnt13(h3) >= 5) m = h3;
    if (m) {
        auto [r0, r1, r2, r3, r4] = top5_from_mask(m);
        return pack_score(5, r0, r1, r2, r3, r4);
    }

    // Straight
    {
        int se = straight_end_from_mask(ranks);
        if (se >= 0) return pack_score(4, se, 0, 0, 0, 0);
    }

    // Three of a kind
    {
        uint16_t trips = (uint16_t)((ge3 & ~ge4) & MASK13);
        if (trips) {
            int tr = msb_index(trips);
            uint16_t kmask = (uint16_t)((ranks & ~(uint16_t)(1u << (uint16_t)tr)) & MASK13);
            int k1 = msb_index(kmask);
            if (k1 >= 0) kmask = (uint16_t)(kmask & ~(uint16_t)(1u << (uint16_t)k1));
            int k2 = msb_index(kmask);
            return pack_score(3, tr, k1, k2, 0, 0);
        }
    }

    // Two pair
    {
        uint16_t pairs = (uint16_t)((ge2 & ~ge3) & MASK13);
        if (pairs && (pairs & (pairs - 1))) {
            int p1 = msb_index(pairs);
            uint16_t pmask = (uint16_t)((pairs & ~(uint16_t)(1u << (uint16_t)p1)) & MASK13);
            int p2 = msb_index(pmask);
            uint16_t kmask = (uint16_t)((ranks &
                ~( (uint16_t)(1u << (uint16_t)p1) | (uint16_t)(1u << (uint16_t)p2) )) & MASK13);
            int k = msb_index(kmask);
            return pack_score(2, p1, p2, k, 0, 0);
        }
    }

    // One pair
    {
        uint16_t pairs = (uint16_t)((ge2 & ~ge3) & MASK13);
        if (pairs) {
            int pr = msb_index(pairs);
            uint16_t kmask = (uint16_t)((ranks & ~(uint16_t)(1u << (uint16_t)pr)) & MASK13);
            int k1 = msb_index(kmask);
            if (k1 >= 0) kmask = (uint16_t)(kmask & ~(uint16_t)(1u << (uint16_t)k1));
            int k2 = msb_index(kmask);
            if (k2 >= 0) kmask = (uint16_t)(kmask & ~(uint16_t)(1u << (uint16_t)k2));
            int k3 = msb_index(kmask);
            return pack_score(1, pr, k1, k2, k3, 0);
        }
    }

    // High card
    int r0 = msb_index(ranks);
    uint16_t m2 = (uint16_t)(ranks & ~(uint16_t)(1u << (uint16_t)(r0 < 0 ? 0 : r0)));
    int r1 = msb_index(m2);
    if (r1 >= 0) m2 = (uint16_t)(m2 & ~(uint16_t)(1u << (uint16_t)r1));
    int r2 = msb_index(m2);
    if (r2 >= 0) m2 = (uint16_t)(m2 & ~(uint16_t)(1u << (uint16_t)r2));
    int r3 = msb_index(m2);
    if (r3 >= 0) m2 = (uint16_t)(m2 & ~(uint16_t)(1u << (uint16_t)r3));
    int r4 = msb_index(m2);
    return pack_score(0, r0, r1, r2, r3, r4);
}