// Correctness proof by exhaustion.
//
// There is no second evaluator to diff against, so the enumeration is checked
// against facts about seven-card poker that hold independently of any
// implementation:
//
//   - the category frequencies over all C(52,7) = 133,784,560 hands
//   - the number of distinct hand values, 4824
//
// The enumeration goes through the eight-wide evaluate_batch and cross-checks
// every score against the scalar evaluate, so the two entry points cannot drift
// apart: the histogram and the distinct count then vouch for both at once.
//
// Between them those pin down both the category cascade and the kicker packing:
// drop a kicker and the distinct count falls, mis-rank a category and the
// histogram moves. A table of hand-versus-hand comparisons covers the
// tie-breaking rules that neither number would notice.

#include "evaluator.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace pokereval;

// Published frequencies for 7-card hands, indexed by Category.
static constexpr uint64_t kExpectedHistogram[9] = {
    23294460,  // high card
    58627800,  // one pair
    31433400,  // two pair
    6461620,   // trips
    6180020,   // straight
    4047644,   // flush
    3473184,   // full house
    224848,    // quads
    41584      // straight flush
};
static constexpr uint64_t kExpectedHands = 133784560;
static constexpr uint64_t kExpectedDistinct = 4824;

// Open-addressed set, sized so 4824 entries stay sparse and cache-resident.
class ScoreSet {
public:
    ScoreSet() : slots_(kSlots, kEmpty) {}

    void insert(uint32_t value) {
        size_t i = (value * 2654435761u) & (kSlots - 1);
        while (slots_[i] != kEmpty) {
            if (slots_[i] == value) return;
            i = (i + 1) & (kSlots - 1);
        }
        slots_[i] = value;
        ++count_;
    }

    uint64_t count() const { return count_; }

private:
    static constexpr size_t kSlots = 1u << 14;
    static constexpr uint32_t kEmpty = 0xFFFFFFFFu;  // no score reaches 2^30
    std::vector<uint32_t> slots_;
    uint64_t count_ = 0;
};

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL  %s\n", what);
        ++failures;
    }
}

// "Ah Kh Qh Jh Th 2c 3d" -> Hand
static Hand parse_hand(const char* text) {
    static constexpr char ranks[] = "23456789TJQKA";
    static constexpr char suits[] = "cdhs";
    Hand hand = 0;
    int cards = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == ' ') continue;
        const char* r = std::strchr(ranks, p[0]);
        const char* s = std::strchr(suits, p[1]);
        if (r == nullptr || s == nullptr) {
            std::printf("  FAIL  unparseable hand: %s\n", text);
            ++failures;
            return 0;
        }
        add_card(hand, make_card(uint32_t(s - suits), uint32_t(r - ranks)));
        ++cards;
        ++p;
    }
    if (cards != 7) {
        std::printf("  FAIL  expected 7 cards, got %d: %s\n", cards, text);
        ++failures;
    }
    return hand;
}

static void expect_category(const char* text, Category want) {
    const Evaluator eval;
    const uint32_t got = score_category(eval.evaluate(parse_hand(text)));
    if (got != uint32_t(want)) {
        std::printf("  FAIL  %s: expected %s, got %s\n", text,
                    category_name(uint32_t(want)), category_name(got));
        ++failures;
    }
}

static void expect_beats(const char* better, const char* worse) {
    const Evaluator eval;
    const Score a = eval.evaluate(parse_hand(better));
    const Score b = eval.evaluate(parse_hand(worse));
    if (!(a > b)) {
        std::printf("  FAIL  expected [%s] > [%s], got 0x%x vs 0x%x\n", better, worse, a, b);
        ++failures;
    }
}

static void expect_ties(const char* left, const char* right) {
    const Evaluator eval;
    const Score a = eval.evaluate(parse_hand(left));
    const Score b = eval.evaluate(parse_hand(right));
    if (a != b) {
        std::printf("  FAIL  expected [%s] == [%s], got 0x%x vs 0x%x\n", left, right, a, b);
        ++failures;
    }
}

static void test_categories() {
    std::printf("categories\n");
    expect_category("Ah Kh Qh Jh Th 2c 3d", Category::StraightFlush);
    expect_category("5h 4h 3h 2h Ah Kc Qd", Category::StraightFlush);
    expect_category("7c 7d 7h 7s Ac Kd Qh", Category::Quads);
    expect_category("9c 9d 9h 4c 4d 2s 3h", Category::FullHouse);
    expect_category("9c 9d 9h 4c 4d 4s 2h", Category::FullHouse);  // two trips
    expect_category("Ac Jc 8c 5c 2c Kd Qh", Category::Flush);
    expect_category("5c 4d 3h 2s Ac Kd Qh", Category::Straight);
    expect_category("7c 7d 7h Ac Kd 4s 2h", Category::Trips);
    expect_category("7c 7d 5c 5d Ah 4s 2c", Category::TwoPair);
    expect_category("7c 7d 5c 5d 3h 3s Ac", Category::TwoPair);  // three pairs
    expect_category("7c 7d Ac Kd Qh 4s 2c", Category::Pair);
    expect_category("Ac Kd Qh Js 9c 3d 2s", Category::HighCard);
}

static void test_ordering() {
    std::printf("ordering\n");
    // Category ladder.
    expect_beats("Ah Kh Qh Jh Th 2c 3d", "7c 7d 7h 7s Ac Kd Qh");
    expect_beats("7c 7d 7h 7s Ac Kd Qh", "9c 9d 9h 4c 4d 2s 3h");
    expect_beats("9c 9d 9h 4c 4d 2s 3h", "Ac Jc 8c 5c 2c Kd Qh");
    expect_beats("Ac Jc 8c 5c 2c Kd Qh", "5c 4d 3h 2s Ac Kd Qh");
    expect_beats("5c 4d 3h 2s Ac Kd Qh", "7c 7d 7h Ac Kd 4s 2h");
    expect_beats("7c 7d 7h Ac Kd 4s 2h", "7c 7d 5c 5d Ah 4s 2c");
    expect_beats("7c 7d 5c 5d Ah 4s 2c", "7c 7d Ac Kd Qh 4s 2c");
    expect_beats("7c 7d Ac Kd Qh 4s 2c", "Ac Kd Qh Js 9c 3d 2s");

    // The wheel is the worst straight and the worst straight flush.
    expect_beats("6h 5h 4h 3h 2h Kc Qd", "5h 4h 3h 2h Ah Kc Qd");
    expect_beats("6c 5d 4h 3s 2c Kd Qh", "5c 4d 3h 2s Ac Kd Qh");

    // Kickers, one per category that has them.
    expect_beats("7c 7d 7h 7s Ac 3d 2h", "7c 7d 7h 7s Kc 3d 2h");
    expect_beats("9c 9d 9h 4c 4d 2s 3h", "8c 8d 8h Ac Ad 2s 3h");  // trip rank first
    expect_beats("9c 9d 9h 4c 4d 2s 3h", "9c 9d 9s 3c 3d 2s 4h");  // then pair rank
    expect_beats("Ac Kc Qc Jc 9c 3d 2s", "Ac Kc Qc Jc 8c 3d 2s");
    expect_beats("7c 7d 7h Ac Kd 4s 2h", "7c 7d 7h Ac Qd 4s 2h");
    expect_beats("7c 7d 5c 5d Ah 4s 2c", "7c 7d 5c 5d Kh 4s 2c");
    expect_beats("7c 7d Ac Kd Qh 4s 2c", "7c 7d Ac Kd Jh 4s 2c");
    expect_beats("Ac Kd Qh Js 9c 3d 2s", "Ac Kd Qh Js 8c 3d 2s");

    // Only the top five cards count, so the sixth and seventh are ignored.
    expect_ties("Ac Kd Qh Js 9c 3d 2s", "Ac Kd Qh Js 9c 5d 4s");
    expect_ties("7c 7d Ac Kd Qh 4s 2c", "7c 7d Ac Kd Qh 5s 3c");

    // With three pairs the third pair's rank is itself a legitimate kicker, so
    // these two hands hold the same five cards: 7 7 5 5 3.
    expect_ties("7c 7d 5c 5d 3h 3s 2c", "7c 7d 5c 5d 2c 2d 3h");
}

static void test_exhaustive() {
    std::printf("exhaustive enumeration of C(52,7)\n");

    Card deck[52];
    size_t n = 0;
    for (uint32_t suit = 0; suit < SuitCount; ++suit) {
        for (uint32_t rank = 0; rank < RankCount; ++rank) deck[n++] = make_card(suit, rank);
    }

    Hand bit[52];
    for (size_t i = 0; i < 52; ++i) bit[i] = card_bit(deck[i]);

    const Evaluator eval;
    ScoreSet distinct;
    uint64_t histogram[9] = {};
    uint64_t hands = 0;
    uint64_t disagreements = 0;

    // Buffered so the batch kernel gets its eight-hand blocks. 1024 is not
    // load-bearing, it just keeps the buffers in L1.
    constexpr size_t kBuffer = 1024;
    std::vector<Hand> buffer(kBuffer);
    std::vector<Score> scores(kBuffer);
    size_t held = 0;

    const auto drain = [&] {
        eval.evaluate_batch(buffer.data(), scores.data(), held);
        for (size_t i = 0; i < held; ++i) {
            const Score s = scores[i];
            if (s != eval.evaluate(buffer[i])) ++disagreements;
            ++histogram[score_category(s)];
            distinct.insert(s);
            ++hands;
        }
        held = 0;
    };

    for (int a = 0; a < 52; ++a) { const Hand ha = bit[a];
    for (int b = a + 1; b < 52; ++b) { const Hand hb = ha | bit[b];
    for (int c = b + 1; c < 52; ++c) { const Hand hc = hb | bit[c];
    for (int d = c + 1; d < 52; ++d) { const Hand hd = hc | bit[d];
    for (int e = d + 1; e < 52; ++e) { const Hand he = hd | bit[e];
    for (int f = e + 1; f < 52; ++f) { const Hand hf = he | bit[f];
    for (int g = f + 1; g < 52; ++g) {
        buffer[held++] = hf | bit[g];
        if (held == kBuffer) drain();
    }}}}}}}
    drain();

    check(hands == kExpectedHands, "hand count");
    std::printf("  %-14s %10llu  %s\n", "batch vs scalar", (unsigned long long)disagreements,
                disagreements == 0 ? "ok" : "MISMATCH");
    check(disagreements == 0, "batch path agrees with scalar path");
    for (uint32_t i = 0; i < 9; ++i) {
        const bool ok = histogram[i] == kExpectedHistogram[i];
        std::printf("  %-14s %10llu  %s\n", category_name(i),
                    (unsigned long long)histogram[i], ok ? "ok" : "MISMATCH");
        if (!ok) ++failures;
    }
    std::printf("  %-14s %10llu  %s\n", "distinct", (unsigned long long)distinct.count(),
                distinct.count() == kExpectedDistinct ? "ok" : "MISMATCH");
    check(distinct.count() == kExpectedDistinct, "distinct hand values");
}

// evaluate_batch hands any leftover under eight to the scalar path, so every
// remainder length needs exercising rather than assuming.
static void test_batch_tails() {
    std::printf("batch tail lengths\n");
    const Evaluator eval;
    std::vector<Hand> in(64);
    std::vector<Score> out(64);
    for (size_t len = 0; len <= 40; ++len) {
        for (size_t i = 0; i < len; ++i) {
            Hand h = 0;
            for (uint32_t k = 0; k < 7; ++k) {
                add_card(h, make_card((uint32_t(i) + k) & 3u, uint32_t((i * 5 + k * 3) % 13)));
            }
            // The construction can collide and produce fewer than seven cards,
            // which is fine here: both paths must agree on whatever it produces.
            in[i] = h;
        }
        eval.evaluate_batch(in.data(), out.data(), len);
        for (size_t i = 0; i < len; ++i) {
            if (out[i] != eval.evaluate(in[i])) {
                std::printf("  FAIL  batch/scalar disagree at length %zu, index %zu\n", len, i);
                ++failures;
            }
        }
    }
}

int main() {
    test_categories();
    test_ordering();
    test_batch_tails();
    test_exhaustive();
    if (failures == 0) {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
}
