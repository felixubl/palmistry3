// equity_calculator.hpp
#pragma once
#include "evaluator.hpp"
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <functional>

// Represents two hole cards with rank information
struct HeroHand {
    int rank1, rank2;    // 0-12 (2-A)
    int suit1, suit2;    // 0-3 (internal suit assignment)
    bool isPair;
};

// Board state with 0-5 cards
struct Board {
    Hand hand = empty_hand();
    int numCards = 0;
    std::vector<std::pair<int,int>> cards; // (suit, rank) pairs
};

// Result for a single opponent hand matchup
struct HandMatchup {
    int opp_rank1, opp_rank2;
    int opp_suit1, opp_suit2;
    double winRate;
    double tieRate;
    uint64_t wins = 0;
    uint64_t ties = 0;
    uint64_t total = 0;
};

// Overall equity calculation results
struct EquityResult {
    std::vector<HandMatchup> matchups;
    double overallWinRate;
    double overallTieRate;
    uint64_t totalScenarios;
    uint64_t totalWins;
    uint64_t totalTies;
    
    // Statistics
    double avgWinRate;
    double minWinRate;
    double maxWinRate;
    int numPossibleOpponents;
};

// Progress callback function type
using ProgressCallback = std::function<void(int current, int total, double elapsed_ms)>;

class EquityCalculator {
private:
    // Helper to add a card to a hand
    static inline void addCard(Hand& h, int suit, int rank) {
        h[suit] |= (1u << rank);
    }
    
    // Check if a card conflicts with existing cards
    static inline bool cardConflicts(const Hand& h, int suit, int rank) {
        return (h[suit] & (1u << rank)) != 0;
    }
    
    // Optimized runout evaluators for each street
    static void evaluateRiver(const Hand& heroBase, const Hand& oppBase, HandMatchup& matchup);
    static void evaluateTurn(const Hand& heroBase, const Hand& oppBase, const Hand& usedCards, HandMatchup& matchup);
    static void evaluateFlop(const Hand& heroBase, const Hand& oppBase, const Hand& usedCards, HandMatchup& matchup);
    static void evaluatePreflop(const Hand& heroBase, const Hand& oppBase, const Hand& usedCards, HandMatchup& matchup);

public:
    // Main equity calculation function
    static EquityResult calculateEquity(const HeroHand& hero, const Board& board, bool showProgress = false);
    
    // Version with custom progress callback
    static EquityResult calculateEquityWithCallback(const HeroHand& hero, const Board& board, 
                                                    ProgressCallback callback = nullptr);
    
    // Helper to create a HeroHand from ranks and suits
    static HeroHand makeHeroHand(int rank1, int suit1, int rank2, int suit2);
    
    // Helper to create a Board from cards
    static Board makeBoard(const std::vector<std::pair<int,int>>& cards);
};
