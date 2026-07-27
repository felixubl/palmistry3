// Table-free 7-card Texas Hold'em evaluator.
//
// Descended from github.com/felixubl/palmistry2 (GPL-3.0). Zero lookup tables:
// the hand's category and its kickers both fall out of bit tricks on a single
// 64-bit word.
//
// Requires arm64 (NEON) for the flush test.

#pragma once

#include <arm_neon.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace pokereval {

// ---------------------------------------------------------------------------
// 1. Representation
// ---------------------------------------------------------------------------
//
// Hand : one uint64_t holding four 16-bit lanes, one per suit. Within a lane,
//        bit r is set if the hand holds that rank in that suit. Only 13 of each
//        16 bits are ever used.
//
// Card : suit * 16 + rank, i.e. the card's code IS its bit index in Hand, so
//        card_bit is a single shift and building a Hand needs no division.
//        rank 0 = Two ... 12 = Ace.
//
// Score: packed uint32_t, see section 5. Comparing two hands is one integer
//        compare.

using Card = uint8_t;
using Hand = uint64_t;
using Score = uint32_t;

constexpr uint32_t RankCount = 13;
constexpr uint32_t SuitCount = 4;
constexpr uint32_t SuitLaneBits = 16;
constexpr uint16_t RankMask = uint16_t((1u << RankCount) - 1u);

// Ace-low straight: A + 2 + 3 + 4 + 5, i.e. bit 12 plus bits 0..3.
constexpr uint16_t WheelMask = uint16_t((1u << 12) | (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3));

enum class Category : uint32_t {
    HighCard = 0,
    Pair = 1,
    TwoPair = 2,
    Trips = 3,
    Straight = 4,
    Flush = 5,
    FullHouse = 6,
    Quads = 7,
    StraightFlush = 8
};

inline constexpr Card make_card(uint32_t suit, uint32_t rank) noexcept {
    return Card(suit * SuitLaneBits + rank);
}

inline uint32_t card_rank(Card card) noexcept { return uint32_t(card) & 0xFu; }
inline uint32_t card_suit(Card card) noexcept { return uint32_t(card) >> 4; }
inline Hand card_bit(Card card) noexcept { return Hand(1) << card; }

inline void add_card(Hand& hand, Card card) noexcept { hand |= card_bit(card); }

inline Hand hand_from_cards(const std::array<Card, 7>& cards) noexcept {
    return card_bit(cards[0]) | card_bit(cards[1]) | card_bit(cards[2]) | card_bit(cards[3])
         | card_bit(cards[4]) | card_bit(cards[5]) | card_bit(cards[6]);
}

// ---------------------------------------------------------------------------
// 2. Bit primitives
// ---------------------------------------------------------------------------

// Clear the lowest set bit. Used to trim a rank mask down to its top k bits,
// which is the whole reason scores hold masks rather than rank indices.
inline uint16_t drop_low(uint16_t mask) noexcept {
    return uint16_t(mask & uint16_t(mask - 1));
}

// Isolate the highest set bit. Unlike clz this is defined at 0, so the callers
// below need no guard branch.
inline uint16_t msb16(uint16_t x) noexcept {
    x = uint16_t(x | (x >> 1));
    x = uint16_t(x | (x >> 2));
    x = uint16_t(x | (x >> 4));
    x = uint16_t(x | (x >> 8));
    return uint16_t(x - (x >> 1));
}

// Bit of the best straight's top card, or 0.
//
//   a = m & m>>1   two in a row
//   b = a & a>>2   four in a row
//   b & m>>4       five in a row, marked at the run's LOWEST rank
//
// so the highest such bit shifted up 4 is the best top card. The wheel is not a
// contiguous run (the ace sits at bit 12) and needs its own test; it tops out at
// the Five, and any real straight outranks it, so max picks the right one.
inline uint16_t straight_bit(uint16_t mask) noexcept {
    const uint16_t a = uint16_t(mask & (mask >> 1));
    const uint16_t b = uint16_t(a & (a >> 2));
    const uint16_t starts = uint16_t(b & (mask >> 4));
    const uint16_t high = uint16_t(msb16(starts) << 4);
    const uint16_t wheel = uint16_t((mask & WheelMask) == WheelMask ? (1u << 3) : 0);
    return high > wheel ? high : wheel;
}

// ---------------------------------------------------------------------------
// 3. Rank multiplicity
// ---------------------------------------------------------------------------
//
// Fold the four suit lanes together while accumulating "this rank has been seen
// once / twice / three times / four times". Thirteen ranks are resolved at a
// time, so nothing is ever counted card by card.

struct Multiplicity {
    uint16_t ones;    // rank appears at least once
    uint16_t twos;    // at least twice
    uint16_t threes;  // at least three times
    uint16_t quads;   // all four suits
};

inline Multiplicity multiplicity(Hand hand) noexcept {
    const uint16_t s0 = uint16_t(hand & RankMask);
    const uint16_t s1 = uint16_t((hand >> 16) & RankMask);
    const uint16_t s2 = uint16_t((hand >> 32) & RankMask);
    const uint16_t s3 = uint16_t((hand >> 48) & RankMask);

    uint16_t ones = s0;
    uint16_t twos = uint16_t(ones & s1);
    ones = uint16_t(ones | s1);
    uint16_t threes = uint16_t(twos & s2);
    twos = uint16_t(twos | uint16_t(ones & s2));
    ones = uint16_t(ones | s2);
    const uint16_t quads = uint16_t(threes & s3);
    threes = uint16_t(threes | uint16_t(twos & s3));
    twos = uint16_t(twos | uint16_t(ones & s3));
    ones = uint16_t(ones | s3);

    return Multiplicity{ones, twos, threes, quads};
}

// ---------------------------------------------------------------------------
// 4. Flush detection
// ---------------------------------------------------------------------------

struct FlushInfo {
    uint16_t mask;   // ranks held in the flush suit
    uint32_t count;  // how many, 5..7
    bool ok;
};

// All four lane popcounts in one NEON pass. Scalar __builtin_popcount on arm64
// compiles to an fmov/cnt/uaddlv/fmov round trip through the vector unit, and
// asking for it four times means four of those; cnt + uaddlp gets every lane at
// once, after which the >=5 test, lane select and lane count are integer ops.
inline FlushInfo flush_info(Hand hand) noexcept {
    const uint8x8_t v = vreinterpret_u8_u64(vcreate_u64(hand));
    const uint64_t counts = vget_lane_u64(vreinterpret_u64_u16(vpaddl_u8(vcnt_u8(v))), 0);
    // count >= 5 iff bit 3 of (count + 3) is set, and count + 3 <= 10 never
    // carries into the neighbouring lane.
    const uint64_t hit = (counts + 0x0003000300030003ull) & 0x0008000800080008ull;
    // Setting bit 63 keeps ctz defined when there is no flush; `ok` discards the
    // bogus lane it selects.
    const uint32_t shift = uint32_t(__builtin_ctzll(hit | (1ull << 63))) & ~15u;

    FlushInfo f;
    f.ok = hit != 0;
    f.mask = uint16_t((hand >> shift) & RankMask);
    f.count = uint32_t((counts >> shift) & 0xFu);
    return f;
}

// A flush mask holds 5, 6 or 7 ranks; keep the top 5.
inline uint16_t keep_top5(uint16_t mask, uint32_t count) noexcept {
    const uint16_t a = drop_low(mask);
    const uint16_t b = drop_low(a);
    return count >= 7 ? b : (count >= 6 ? a : mask);
}

// ---------------------------------------------------------------------------
// 5. Score
// ---------------------------------------------------------------------------
//
//   score = category << 26 | primary << 13 | secondary
//
// where primary and secondary are 13-bit RANK MASKS rather than lists of rank
// indices. Within a category both always hold a fixed number of bits, and for
// two masks of equal popcount, integer order is exactly lexicographic order of
// their descending rank lists. So ordering is preserved while the five serial
// clz steps that a nibble-packed score needs to extract kickers disappear:
// trimming a mask to its top k is `m &= m - 1`.
//
// Per category, primary / secondary hold:
//
//   straight flush  top card       -
//   quads           quad rank      kicker
//   full house      trip rank      pair rank
//   flush           top 5 ranks    -
//   straight        top card       -
//   trips           trip rank      2 kickers
//   two pair        2 pair ranks   1 kicker
//   one pair        pair rank      3 kickers
//   high card       -              top 5 ranks

inline Score pack_score(uint32_t category, uint32_t primary, uint32_t secondary) noexcept {
    return (category << 26) | (primary << 13) | secondary;
}

inline uint32_t score_category(Score score) noexcept { return score >> 26; }
inline uint16_t score_primary(Score score) noexcept { return uint16_t((score >> 13) & RankMask); }
inline uint16_t score_secondary(Score score) noexcept { return uint16_t(score & RankMask); }

// ---------------------------------------------------------------------------
// 6. The evaluator
// ---------------------------------------------------------------------------

struct Evaluator {
    static constexpr size_t table_bytes = 0;
    static constexpr const char* name = "no-LUT mask-score";

    Score evaluate(Hand hand) const noexcept {
        const Multiplicity m = multiplicity(hand);
        const FlushInfo f = flush_info(hand);

        if (f.ok) {
            const uint16_t sf = straight_bit(f.mask);
            if (sf != 0) return pack_score(uint32_t(Category::StraightFlush), sf, 0);
        }

        // Quads imply three-of-a-kind, so one test gates both, and the 92.4% of
        // hands with no trips at all skip the whole block.
        if (m.threes != 0) {
            if (m.quads != 0) {
                return pack_score(uint32_t(Category::Quads), m.quads, msb16(uint16_t(m.ones ^ m.quads)));
            }
            // A second set of trips also has 2+ copies, so it falls out of
            // `twos` for free and no separate two-trips case is needed.
            const uint16_t trip = msb16(m.threes);
            const uint16_t pair = uint16_t(m.twos ^ trip);
            if (pair != 0) {
                return pack_score(uint32_t(Category::FullHouse), trip, msb16(pair));
            }
        }

        if (f.ok) {
            return pack_score(uint32_t(Category::Flush), keep_top5(f.mask, f.count), 0);
        }

        const uint16_t straight = straight_bit(m.ones);
        if (straight != 0) {
            return pack_score(uint32_t(Category::Straight), straight, 0);
        }

        // Everything left -- trips, two pair, one pair, high card, 89.6% of all
        // hands -- is the same computation. Let P be the ranks held more than
        // once and K the kickers; then in every one of those four cases K is
        // "the other ranks, minus the two lowest", the sole exception being
        // three pairs, where only one is dropped because the third pair's rank
        // is itself a legitimate kicker.
        const uint16_t pair_lo = drop_low(m.twos);
        const bool three_pairs = drop_low(pair_lo) != 0;
        const uint16_t primary = three_pairs ? pair_lo : m.twos;
        const uint16_t rest = uint16_t(m.ones ^ primary);
        const uint16_t trimmed = drop_low(rest);
        const uint16_t secondary = three_pairs ? trimmed : drop_low(trimmed);
        const uint32_t category =
            uint32_t(m.twos != 0) + uint32_t(pair_lo != 0) + 2u * uint32_t(m.threes != 0);
        return pack_score(category, primary, secondary);
    }

    Score evaluate(const std::array<Card, 7>& cards) const noexcept {
        return evaluate(hand_from_cards(cards));
    }
};

// ---------------------------------------------------------------------------
// 7. Debug printing
// ---------------------------------------------------------------------------

inline const char* category_name(uint32_t category) noexcept {
    static constexpr const char* names[] = {
        "high card", "one pair", "two pair", "trips", "straight",
        "flush", "full house", "quads", "straight flush"
    };
    return category < 9 ? names[category] : "unknown";
}

inline char rank_char(uint32_t rank) noexcept { return "23456789TJQKA"[rank]; }
inline char suit_char(uint32_t suit) noexcept { return "cdhs"[suit]; }

inline std::string card_to_string(Card card) {
    return std::string{rank_char(card_rank(card)), suit_char(card_suit(card))};
}

inline std::string cards_to_string(const std::array<Card, 7>& cards) {
    std::string text;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (i != 0) text.push_back(' ');
        text += card_to_string(cards[i]);
    }
    return text;
}

// Ranks in a mask, highest first.
inline std::string ranks_to_string(uint16_t mask) {
    std::string text;
    for (int r = int(RankCount) - 1; r >= 0; --r) {
        if (mask & uint16_t(1u << r)) text.push_back(rank_char(uint32_t(r)));
    }
    return text;
}

inline std::string score_to_string(Score score) {
    std::string text = category_name(score_category(score));
    const std::string primary = ranks_to_string(score_primary(score));
    const std::string secondary = ranks_to_string(score_secondary(score));
    if (!primary.empty()) text += " " + primary;
    if (!secondary.empty()) text += (primary.empty() ? " " : " + ") + secondary;
    char raw[24];
    std::snprintf(raw, sizeof raw, " raw=0x%06x", score);
    return text + raw;
}

} // namespace pokereval
