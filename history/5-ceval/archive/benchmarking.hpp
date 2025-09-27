#pragma once
#include <vector>
#include <cstdint>
#include "evaluator.hpp"

struct BenchResult {
    double   seconds{};
    double   mhps{};
    uint32_t checksum{};
};

BenchResult eval_sequential(const std::vector<Hand>& hands);
BenchResult eval_parallel(const std::vector<Hand>& hands, unsigned threads = 0);
