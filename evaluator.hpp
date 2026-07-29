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

// Isolate the highest set bit. Every caller has already branched on its argument
// being non-zero, so clz needs no guard and the four-step or-shift smear this
// replaced collapses to two instructions. Passing 0 is undefined.
inline uint16_t msb16(uint16_t x) noexcept {
    return uint16_t(0x80000000u >> __builtin_clz(uint32_t(x)));
}

// Bit of the best straight's top card, or 0.
//
// First widen to a 14-bit frame in which rank r sits at bit r+1 and the ace is
// repeated at bit 0. That makes the wheel an ordinary contiguous run, so no
// separate test and no max are needed. Then
//
//   a = m & m>>1     two in a row
//   b = a & a>>2     four in a row
//   m & b<<4         five in a row, marked at the run's TOP rank
//
// Marking the top rather than the bottom costs nothing, one shifted-operand AND
// either way, and it buys the sentinel: bit 0 of the widened frame is the
// repeated ace, which can never be a run's top, so `| 1` keeps clz defined when
// there is no straight and shifts back out to zero. Without it the highest set
// bit needs a four-step or-shift smear, five instructions of depth five on a
// chain that every non-flush hand runs and that the score select waits on.
inline uint16_t straight_bit(uint16_t mask) noexcept {
    const uint32_t m = uint32_t(mask) << 1 | uint32_t(mask >> 12);
    const uint32_t a = m & (m >> 1);
    const uint32_t b = a & (a >> 2);
    const uint32_t tops = (m & (b << 4)) | 1u;
    return uint16_t((0x80000000u >> __builtin_clz(tops)) >> 1);
}

// ---------------------------------------------------------------------------
// 3. Rank multiplicity
// ---------------------------------------------------------------------------
//
// The four answers wanted, "this rank has been seen at least once / twice /
// three times / four times", are the four threshold functions of the four suit
// lanes. That is a 4-input sorting network, and a network is a tree rather than
// the serial carry chain a running counter suggests, so pair the lanes first:
//
//   a = s0&s1   b = s0|s1   c = s2&s3   d = s2|s3
//
// Writing x for how many of {s0,s1} hold the rank and y for the same over
// {s2,s3}, that is a = x>=2, b = x>=1, c = y>=2, d = y>=1, and so
//
//   ones = b|d          quads = a&c
//   twos = (a|c)|(b&d)  threes = (a|c)&(b&d)
//
// twos and threes sharing both operands is what makes this cheap: with u = a|c
// and v = b&d they are just u|v and u&v. Nor do the lanes need extracting one by
// one, since pairing (s0,s1) and (s2,s3) is a shift by 16 and pairing the pairs
// is a shift by 32, and arm64 folds a shifted second operand into the logical op
// for free. Thirteen ranks are still resolved at a time, so nothing is ever
// counted card by card, but the depth is 4 rather than 7.

struct Multiplicity {
    uint16_t ones;    // rank appears at least once
    uint16_t twos;    // at least twice
    uint16_t threes;  // at least three times
    uint16_t quads;   // all four suits
};

inline Multiplicity multiplicity(Hand hand) noexcept {
    const Hand p = hand & (hand >> 16);
    const Hand q = hand | (hand >> 16);

    const uint32_t a = uint32_t(p) & RankMask, c = uint32_t(p >> 32) & RankMask;
    const uint32_t b = uint32_t(q) & RankMask, d = uint32_t(q >> 32) & RankMask;

    const uint32_t u = a | c;  // one of the two pairs is doubled up
    const uint32_t v = b & d;  // both pairs are represented

    return Multiplicity{uint16_t(b | d), uint16_t(u | v), uint16_t(u & v), uint16_t(a & c)};
}

// ---------------------------------------------------------------------------
// 4. Flush detection
// ---------------------------------------------------------------------------

struct FlushInfo {
    uint64_t counts;  // four 16-bit lanes, each the popcount of that suit
    uint64_t hit;     // the flushing suit's lane all-ones, or zero throughout
};

// All four lane popcounts in one NEON pass. Scalar __builtin_popcount on arm64
// compiles to an fmov/cnt/uaddlv/fmov round trip through the vector unit, and
// asking for it four times means four of those; cnt + uaddlp gets every lane at
// once. The >=5 test then stays in the vector unit too: the lanes are already
// there, so one cmhs against a splat of 5 costs less than moving them out and
// testing with a (count+3)&8 carry trick, and because a hit lane comes back
// all-ones rather than as a single bit part-way up, ctz lands exactly on the
// lane boundary and needs no realigning.
//
// Only the test happens here. Which suit won and what it holds costs a ctz and
// two shifts that 96.9% of hands never need, so it lives behind the caller's
// branch. Left here, clang hoists it above that branch and every hand pays.
inline FlushInfo flush_info(Hand hand) noexcept {
    const uint8x8_t v = vreinterpret_u8_u64(vcreate_u64(hand));
    const uint16x4_t counts = vpaddl_u8(vcnt_u8(v));
    const uint64_t hit =
        vget_lane_u64(vreinterpret_u64_u16(vcge_u16(counts, vdup_n_u16(5))), 0);
    return FlushInfo{vget_lane_u64(vreinterpret_u64_u16(counts), 0), hit};
}

// Byte offset of the flushing suit's lane. Only valid when hit != 0, which is
// what makes the ctz guard unnecessary.
inline uint32_t flush_shift(uint64_t hit) noexcept {
    return uint32_t(__builtin_ctzll(hit));
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
    return (secondary | (primary << 13)) | (category << 26);
}

inline uint32_t score_category(Score score) noexcept { return score >> 26; }
inline uint16_t score_primary(Score score) noexcept { return uint16_t((score >> 13) & RankMask); }
inline uint16_t score_secondary(Score score) noexcept { return uint16_t(score & RankMask); }

// ---------------------------------------------------------------------------
// 6. Eight-lane primitives
// ---------------------------------------------------------------------------
//
// The batch path below evaluates eight hands per iteration, so every primitive
// above needs a form that works on eight 16-bit lanes at once. They are the same
// expressions; only the spelling changes. Two differences are worth noting:
// NEON has no shifted-register operand, so tricks that lean on that in the
// scalar code buy nothing here, and NEON does have a lane-wise clz, which makes
// the highest-set-bit cheaper here than there.

inline uint16x8_t vdrop_low(uint16x8_t mask) noexcept {
    return vandq_u16(mask, vsubq_u16(mask, vdupq_n_u16(1)));
}

// Population count of each lane: one cnt over the bytes, one pairwise widening
// add to rejoin each lane's two halves.
inline uint16x8_t vpopcount16(uint16x8_t x) noexcept {
    return vpaddlq_u8(vcntq_u8(vreinterpretq_u8_u16(x)));
}

// Isolate the highest set bit. A zero lane gives clz 16, hence a shift of -1,
// hence 1 >> 1 == 0, so unlike the scalar msb16 this one is defined at zero and
// needs no sentinel.
inline uint16x8_t vmsb(uint16x8_t x) noexcept {
    const int16x8_t sh = vsubq_s16(vdupq_n_s16(15), vreinterpretq_s16_u16(vclzq_u16(x)));
    return vshlq_u16(vdupq_n_u16(1), sh);
}

// Bit of each lane's best straight, or 0. Same widened 14-bit frame as
// straight_bit, but marking each run at its lowest rank, because the reason the
// scalar version marks the top is to free bit 0 for a clz sentinel and vmsb
// does not need one.
inline uint16x8_t vstraight_bit(uint16x8_t mask) noexcept {
    const uint16x8_t m = vorrq_u16(vshlq_n_u16(mask, 1), vshrq_n_u16(mask, 12));
    const uint16x8_t a = vandq_u16(m, vshrq_n_u16(m, 1));
    const uint16x8_t b = vandq_u16(a, vshrq_n_u16(a, 2));
    const uint16x8_t starts = vandq_u16(b, vshrq_n_u16(m, 4));
    return vshlq_n_u16(vmsb(starts), 3);
}

// Lane-wise "is this mask non-zero", as 0 or 0xFFFF.
inline uint16x8_t vany(uint16x8_t x) noexcept { return vtstq_u16(x, x); }

// ---------------------------------------------------------------------------
// 7. The evaluator
// ---------------------------------------------------------------------------

struct Evaluator {
    static constexpr size_t table_bytes = 0;
    static constexpr const char* name = "no-LUT mask-score";

    // always_inline because clang declines at -O3: the body is over its size
    // budget, so every call was paying a bl, a ret, a `this` it never reads and
    // a 64-bit literal that should have been hoisted out of the caller's loop.
    [[gnu::always_inline]] Score evaluate(Hand hand) const noexcept {
        const FlushInfo f = flush_info(hand);

        // Five cards in one suit leaves two cards outside it, and quads need
        // three of those while a full house needs three between its trip and
        // its pair. So a flush hand can be nothing better than a straight
        // flush, and one branch settles the whole suited half of the cascade:
        // the lane select, the rank mask and the count all sink in here, off
        // the 96.9% of hands that are not flushes.
        if (f.hit != 0) {
            const uint32_t shift = flush_shift(f.hit);
            const uint16_t mask = uint16_t((hand >> shift) & RankMask);
            const uint16_t sf = straight_bit(mask);
            if (sf != 0) return pack_score(uint32_t(Category::StraightFlush), sf, 0);
            const uint32_t count = uint32_t((f.counts >> shift) & 0xFu);
            return pack_score(uint32_t(Category::Flush), keep_top5(mask, count), 0);
        }

        const Multiplicity m = multiplicity(hand);
        const uint16_t straight = straight_bit(m.ones);

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

            // The block returns unconditionally rather than dropping bare trips
            // into the tail below. Given that we are here at all the hand is a
            // full house two times in three, which is the one test in the
            // evaluator whose direction the predictor cannot learn, so it is
            // paid for with selects instead. Bare trips means `twos` is the
            // trip rank alone, so the primary is the trip rank either way and
            // only the category and the kickers differ. Only bare trips can be
            // beaten by a straight: a full house or quads leaves at most four
            // distinct ranks, too few for a run of five.
            const uint16_t kickers = drop_low(drop_low(uint16_t(m.ones ^ trip)));
            const uint32_t category =
                pair != 0 ? uint32_t(Category::FullHouse) : uint32_t(Category::Trips);
            const Score multiples = pack_score(category, trip, pair != 0 ? msb16(pair) : kickers);
            const bool run_wins = (pair == 0) & (straight != 0);
            return run_wins ? pack_score(uint32_t(Category::Straight), straight, 0) : multiples;
        }

        if (straight != 0) {
            return pack_score(uint32_t(Category::Straight), straight, 0);
        }

        // Everything left -- two pair, one pair, high card, 84.7% of all hands
        // -- is the same computation. Let P be the ranks held more than once and
        // K the kickers; then in each of those three cases K is "the other
        // ranks, minus the two lowest", the sole exception being three pairs,
        // where only one is dropped because the third pair's rank is itself a
        // legitimate kicker. That second drop is spelt as a subtrahend rather
        // than a select so the three-pairs flag is read once into a 0/1 register
        // instead of twice into two csels.
        const uint16_t pair_lo = drop_low(m.twos);
        const bool three_pairs = drop_low(pair_lo) != 0;
        const uint16_t primary = three_pairs ? pair_lo : m.twos;
        const uint16_t rest = uint16_t(m.ones ^ primary);
        const uint16_t trimmed = drop_low(rest);
        const uint16_t again = uint16_t(!three_pairs);
        const uint16_t secondary = uint16_t(trimmed & uint16_t(trimmed - again));
        // Trips leave through the block above, so only high card, one pair and
        // two pair reach here and the `threes` term of the category is gone.
        const uint32_t category = uint32_t(m.twos != 0) + uint32_t(pair_lo != 0);
        return pack_score(category, primary, secondary);
    }

    Score evaluate(const std::array<Card, 7>& cards) const noexcept {
        return evaluate(hand_from_cards(cards));
    }

    // -----------------------------------------------------------------------
    // Eight at a time.
    //
    // Same evaluator, eight hands wide. Two things make that pay rather than
    // merely work.
    //
    // The transpose is free. Vector code wants one register per suit holding
    // eight hands, which is the transpose of one word per hand holding four
    // suits, and a transpose normally costs a fistful of shuffles. But eight
    // consecutive Hands are thirty-two consecutive uint16, every fourth of
    // which is the same suit, so `vld4q_u16` -- a four-way de-interleaving load
    // -- lands suit s of hand h in val[s] lane h. The load IS the transpose,
    // for no instructions at all. That is a dividend on the 16-bit lane stride,
    // which was chosen so that a card's code is its own bit index.
    //
    // Being branchless stops being a liability. Eight hands in one register
    // disagree about their category, so the cascade has to become a chain of
    // selects that computes a candidate for every category: exactly the design
    // measured at less than half the scalar speed. Scalar, that work serves one
    // hand. Here it serves eight, and the same arithmetic that sank it is what
    // makes it win. The chain runs weakest category first so the last write
    // wins, and every candidate is only ever selected where it is genuinely
    // that category, so nothing needs clamping.
    //
    // Branches come back, per batch instead of per hand. A lane cannot branch,
    // but eight lanes can agree that none of them needs the rare work. A flush
    // is 3.06% of hands, so 0.969^8 = 78% of batches skip the flush block, which
    // is 37 of the kernel's instructions, for one horizontal max and a branch
    // the predictor gets right nearly four times in five.
    //
    // Do not add a second one over the multiples block. It looks like the same
    // trade and it is not: trips or better is 7.6% of hands, so 0.924^8 = 53% of
    // batches skip, which is a coin flip, and a coin flip is the one thing a
    // branch predictor cannot do anything with. Measured at 27% slower, against
    // a flush guard that is worth about 13%. The frequency that makes a
    // per-batch guard pay is the low one, not the middling one.
    [[gnu::always_inline]] static void evaluate8(const Hand* in, Score* out) noexcept {
        const uint16x8x4_t t = vld4q_u16(reinterpret_cast<const uint16_t*>(in));
        const uint16x8_t s0 = t.val[0], s1 = t.val[1], s2 = t.val[2], s3 = t.val[3];

        // multiplicity()'s sorting network. The lanes arrive already separated,
        // so pairing them needs no shifting at all.
        const uint16x8_t pa = vandq_u16(s0, s1), pb = vorrq_u16(s0, s1);
        const uint16x8_t pc = vandq_u16(s2, s3), pd = vorrq_u16(s2, s3);
        const uint16x8_t u = vorrq_u16(pa, pc), v = vandq_u16(pb, pd);
        const uint16x8_t ones = vorrq_u16(pb, pd);
        const uint16x8_t twos = vorrq_u16(u, v);
        const uint16x8_t threes = vandq_u16(u, v);
        const uint16x8_t quads = vandq_u16(pa, pc);

        // Four cnt/uaddlp pairs cover all four suits of all eight hands, and
        // their horizontal max decides whether this batch touches flushes.
        const uint16x8_t c0 = vpopcount16(s0), c1 = vpopcount16(s1);
        const uint16x8_t c2 = vpopcount16(s2), c3 = vpopcount16(s3);
        const uint16_t widest = vmaxvq_u16(vmaxq_u16(vmaxq_u16(c0, c1), vmaxq_u16(c2, c3)));

        const uint16x8_t st = vstraight_bit(ones);

        // The shared common-category tail, lane for lane with the scalar one
        // except that the ternaries are bsl and the category is summed from
        // compare masks, which are 0 or 0xFFFF, i.e. 0 or -1. Trips does reach
        // this tail, unlike in the scalar path, because here there is no branch
        // to leave through and the `threes` term costs two instructions.
        const uint16x8_t pair_lo = vdrop_low(twos);
        const uint16x8_t three_pairs = vany(vdrop_low(pair_lo));
        const uint16x8_t any_threes = vany(threes);
        uint16x8_t primary = vbslq_u16(three_pairs, pair_lo, twos);
        const uint16x8_t trimmed = vdrop_low(veorq_u16(ones, primary));
        uint16x8_t secondary = vbslq_u16(three_pairs, trimmed, vdrop_low(trimmed));
        uint16x8_t category = vsubq_u16(vsubq_u16(vdupq_n_u16(0), vany(twos)), vany(pair_lo));
        category = vsubq_u16(category, vshlq_n_u16(any_threes, 1));

        const uint16x8_t is_straight = vany(st);
        category = vbslq_u16(is_straight, vdupq_n_u16(4), category);
        primary = vbslq_u16(is_straight, st, primary);
        secondary = vbicq_u16(secondary, is_straight);

        // Quads and a full house both need three of a rank, so one horizontal
        // max gates both. When no lane has trips these selects would all be
        // no-ops anyway, which is what makes skipping them exact.
        const uint16x8_t trip = vmsb(threes);
        const uint16x8_t under = veorq_u16(twos, trip);

        const uint16x8_t is_full = vandq_u16(any_threes, vany(under));
        category = vbslq_u16(is_full, vdupq_n_u16(6), category);
        primary = vbslq_u16(is_full, trip, primary);
        secondary = vbslq_u16(is_full, vmsb(under), secondary);

        const uint16x8_t is_quads = vany(quads);
        category = vbslq_u16(is_quads, vdupq_n_u16(7), category);
        primary = vbslq_u16(is_quads, quads, primary);
        secondary = vbslq_u16(is_quads, vmsb(veorq_u16(ones, quads)), secondary);

        // Flush is applied after quads and the full house, which the scalar
        // cascade cannot do because it returns early. Seven cards cannot hold
        // both, so the order is free, and taking it puts every flush-dependent
        // instruction into one contiguous block that 78% of batches jump over.
        if (widest >= 5) {
            const uint16x8_t five = vdupq_n_u16(5);
            uint16x8_t fmask = vandq_u16(s3, vcgeq_u16(c3, five));
            fmask = vbslq_u16(vcgeq_u16(c2, five), s2, fmask);
            fmask = vbslq_u16(vcgeq_u16(c1, five), s1, fmask);
            fmask = vbslq_u16(vcgeq_u16(c0, five), s0, fmask);
            // A lane whose own hand has no flush leaves fmask zero there, which
            // then fails every test below without needing a separate guard.
            const uint16x8_t fcount = vpopcount16(fmask);
            const uint16x8_t drop1 = vdrop_low(fmask);
            const uint16x8_t top5 = vbslq_u16(vcgeq_u16(fcount, vdupq_n_u16(6)),
                                              vbslq_u16(vcgeq_u16(fcount, vdupq_n_u16(7)),
                                                        vdrop_low(drop1), drop1),
                                              fmask);
            const uint16x8_t is_flush = vcgeq_u16(fcount, five);
            category = vbslq_u16(is_flush, five, category);
            primary = vbslq_u16(is_flush, top5, primary);
            secondary = vbicq_u16(secondary, is_flush);

            const uint16x8_t sf = vstraight_bit(fmask);
            const uint16x8_t is_sf = vany(sf);
            category = vbslq_u16(is_sf, vdupq_n_u16(8), category);
            primary = vbslq_u16(is_sf, sf, primary);
            secondary = vbicq_u16(secondary, is_sf);
        }

        // category << 26 | primary << 13 | secondary. Pre-scaling the category
        // by 10 inside 16 bits lets the widening shift supply the other 16.
        const uint16x8_t cat10 = vshlq_n_u16(category, 10);
        vst1q_u32(out, vorrq_u32(vorrq_u32(vshll_n_u16(vget_low_u16(cat10), 16),
                                           vshll_n_u16(vget_low_u16(primary), 13)),
                                 vmovl_u16(vget_low_u16(secondary))));
        vst1q_u32(out + 4, vorrq_u32(vorrq_u32(vshll_high_n_u16(cat10, 16),
                                               vshll_high_n_u16(primary, 13)),
                                     vmovl_high_u16(secondary)));
    }

    // Score n hands. The eight-wide kernel needs its hands contiguous, so a
    // caller with fewer than eight left over falls back to the scalar path,
    // which produces bit-identical scores.
    void evaluate_batch(const Hand* in, Score* out, size_t n) const noexcept {
        size_t i = 0;
        for (; i + 8 <= n; i += 8) evaluate8(in + i, out + i);
        for (; i < n; ++i) out[i] = evaluate(in[i]);
    }

};

// ---------------------------------------------------------------------------
// 8. Debug printing
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
