// Minimal driver: evaluate 7 cards and print the breakdown.
//
//   make && ./build/eval Ah Kh Qh Jh Th 2c 3d
//
// With no arguments it runs a handful of illustrative hands.

#include "evaluator.hpp"

#include <cstdio>
#include <cstring>

using namespace pokereval;

// "Ah" -> card, or -1.
static int parse_card(const char* text) {
    if (std::strlen(text) != 2) return -1;
    static constexpr char ranks[] = "23456789TJQKA";
    static constexpr char suits[] = "cdhs";
    const char* r = std::strchr(ranks, text[0]);
    const char* s = std::strchr(suits, text[1]);
    if (r == nullptr || s == nullptr) return -1;
    return int(make_card(uint32_t(s - suits), uint32_t(r - ranks)));
}

static bool show(const char* const* words) {
    std::array<Card, 7> cards{};
    for (int i = 0; i < 7; ++i) {
        const int card = parse_card(words[i]);
        if (card < 0) {
            std::fprintf(stderr, "bad card: %s (expected e.g. Ah, Td, 2c)\n", words[i]);
            return false;
        }
        cards[size_t(i)] = Card(card);
    }
    const Evaluator eval;
    std::printf("%-24s -> %s\n", cards_to_string(cards).c_str(),
                score_to_string(eval.evaluate(cards)).c_str());
    return true;
}

int main(int argc, char** argv) {
    if (argc == 8) return show(argv + 1) ? 0 : 1;
    if (argc != 1) {
        std::fprintf(stderr, "usage: %s <7 cards, e.g. Ah Kh Qh Jh Th 2c 3d>\n", argv[0]);
        return 1;
    }

    static const char* samples[][7] = {
        {"Ah", "Kh", "Qh", "Jh", "Th", "2c", "3d"}, // straight flush (broadway)
        {"5h", "4h", "3h", "2h", "Ah", "Kc", "Qd"}, // straight flush (wheel)
        {"7c", "7d", "7h", "7s", "Ac", "Kd", "Qh"}, // quads, ace kicker
        {"9c", "9d", "9h", "4c", "4d", "2s", "3h"}, // full house
        {"Ac", "Jc", "8c", "5c", "2c", "Kd", "Qh"}, // flush
        {"5c", "4d", "3h", "2s", "Ac", "Kd", "Qh"}, // wheel straight
        {"7c", "7d", "7h", "Ac", "Kd", "4s", "2h"}, // trips, A-K kickers
        {"7c", "7d", "5c", "5d", "Ah", "4s", "2c"}, // two pair, ace kicker
        {"7c", "7d", "5c", "5d", "3h", "3s", "Ac"}, // three pairs, ace kicker
        {"7c", "7d", "Ac", "Kd", "Qh", "4s", "2c"}, // one pair, A-K-Q kickers
        {"Ac", "Kd", "Qh", "Js", "9c", "3d", "2s"}, // high card
    };

    for (const auto& sample : samples) show(sample);
    return 0;
}
