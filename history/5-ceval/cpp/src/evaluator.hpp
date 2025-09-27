#pragma once
#include <array>
#include <cstdint>

using Hand = std::array<uint16_t, 4>;

// Core API
uint32_t evaluate_u32(const Hand& hand);

// Utilities you already use publicly
Hand     empty_hand();

struct XorShift64 {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint64_t next();
    uint32_t next_u32();
    uint32_t uniform(uint32_t n); // [0, n)
};

Hand     random_hand(XorShift64& rng);
