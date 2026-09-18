// test_equity.cpp
#include "evaluator.hpp"
#include "equity.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <cassert>

// Helper to convert string card notation to rank/suit
std::pair<int, int> parseCard(const std::string& card) {
    static const std::map<char, int> rankMap = {
        {'2', 0}, {'3', 1}, {'4', 2}, {'5', 3}, {'6', 4},
        {'7', 5}, {'8', 6}, {'9', 7}, {'T', 8}, {'J', 9},
        {'Q', 10}, {'K', 11}, {'A', 12}
    };
    static const std::map<char, int> suitMap = {
        {'s', 0}, {'h', 1}, {'d', 2}, {'c', 3}
    };
    
    if (card.length() != 2) {
        throw std::invalid_argument("Invalid card format: " + card);
    }
    
    auto rankIt = rankMap.find(card[0]);
    auto suitIt = suitMap.find(card[1]);
    
    if (rankIt == rankMap.end() || suitIt == suitMap.end()) {
        throw std::invalid_argument("Invalid card: " + card);
    }
    
    return {suitIt->second, rankIt->second};
}

// Convert string notation to HeroHand
HeroHand parseHeroHand(const std::string& hand) {
    if (hand.length() != 4) {
        throw std::invalid_argument("Hero hand must be 4 characters (e.g., 'AsKh')");
    }
    
    auto [suit1, rank1] = parseCard(hand.substr(0, 2));
    auto [suit2, rank2] = parseCard(hand.substr(2, 2));
    
    return EquityCalculator::makeHeroHand(rank1, suit1, rank2, suit2);
}

// Convert vector of string cards to Board
Board parseBoard(const std::vector<std::string>& cards) {
    std::vector<std::pair<int, int>> boardCards;
    for (const auto& card : cards) {
        boardCards.push_back(parseCard(card));
    }
    return EquityCalculator::makeBoard(boardCards);
}

// Pretty print a hand
std::string handToString(int rank1, int suit1, int rank2, int suit2) {
    static const char* ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"};
    static const char* suits[] = {"s", "h", "d", "c"};
    return std::string(ranks[rank1]) + suits[suit1] + ranks[rank2] + suits[suit2];
}

// Print detailed equity results
void printEquityResult(const EquityResult& result) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== Equity Calculation Results ===\n";
    std::cout << "Possible opponent hands: " << result.numPossibleOpponents << "\n";
    std::cout << "Total scenarios evaluated: " << result.totalScenarios << "\n";
    std::cout << "\nOverall Equity:\n";
    std::cout << "  Win Rate: " << (result.overallWinRate * 100) << "%\n";
    std::cout << "  Tie Rate: " << (result.overallTieRate * 100) << "%\n";
    std::cout << "  Loss Rate: " << ((1 - result.overallWinRate - result.overallTieRate) * 100) << "%\n";
    std::cout << "\nStatistics:\n";
    std::cout << "  Average win rate: " << (result.avgWinRate * 100) << "%\n";
    std::cout << "  Best matchup: " << (result.maxWinRate * 100) << "%\n";
    std::cout << "  Worst matchup: " << (result.minWinRate * 100) << "%\n";
    
    // Show top 5 best and worst matchups
    auto sorted = result.matchups;
    std::sort(sorted.begin(), sorted.end(), 
              [](const auto& a, const auto& b) { return a.winRate > b.winRate; });
    
    std::cout << "\nTop 5 Best Matchups:\n";
    const char* ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"};
    const char* suits[] = {"s", "h", "d", "c"};
    
    for (int i = 0; i < std::min(5, (int)sorted.size()); ++i) {
        const auto& m = sorted[i];
        std::cout << "  vs " << ranks[m.opp_rank1] << suits[m.opp_suit1] 
                  << ranks[m.opp_rank2] << suits[m.opp_suit2]
                  << ": Win " << (m.winRate * 100) << "%, Tie " << (m.tieRate * 100) << "%\n";
    }
    
    std::cout << "\nTop 5 Worst Matchups:\n";
    for (int i = std::max(0, (int)sorted.size() - 5); i < (int)sorted.size(); ++i) {
        const auto& m = sorted[i];
        std::cout << "  vs " << ranks[m.opp_rank1] << suits[m.opp_suit1] 
                  << ranks[m.opp_rank2] << suits[m.opp_suit2]
                  << ": Win " << (m.winRate * 100) << "%, Tie " << (m.tieRate * 100) << "%\n";
    }
}

// Benchmark a specific scenario
struct BenchmarkResult {
    std::string scenario;
    double milliseconds;
    int opponentHands;
    uint64_t totalEvaluations;
    double evalPerSecond;
};

BenchmarkResult benchmarkScenario(const std::string& name, const HeroHand& hero, const Board& board, bool showProgress = false) {
    auto start = std::chrono::high_resolution_clock::now();
    
    auto result = EquityCalculator::calculateEquity(hero, board, showProgress);
    
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    BenchmarkResult bench;
    bench.scenario = name;
    bench.milliseconds = ms;
    bench.opponentHands = result.numPossibleOpponents;
    bench.totalEvaluations = result.totalScenarios;
    bench.evalPerSecond = (bench.totalEvaluations / ms) * 1000.0;
    
    return bench;
}

// Test correctness with known scenarios
void runCorrectnessTests() {
    std::cout << "\n=== CORRECTNESS TESTS ===\n\n";
    
    // Test 1: AA vs KK preflop (should be ~80% favorite)
    {
        std::cout << "Test 1: AA vs specific KK preflop\n";
        std::cout << "Calculating (this may take a moment for preflop)...\n";
        HeroHand aa = parseHeroHand("AsAh");
        Board empty = parseBoard({});
        
        // Calculate against all hands with progress
        auto result = EquityCalculator::calculateEquity(aa, empty, true);
        
        // Find KsKh specifically
        bool found = false;
        for (const auto& m : result.matchups) {
            if (m.opp_rank1 == 11 && m.opp_rank2 == 11) { // Kings
                std::cout << "  AA vs " << handToString(m.opp_rank1, m.opp_suit1, m.opp_rank2, m.opp_suit2)
                          << ": Win " << (m.winRate * 100) << "%, Tie " << (m.tieRate * 100) << "%\n";
                
                // AA should win roughly 80-82% vs KK
                assert(m.winRate > 0.78 && m.winRate < 0.84);
                found = true;
            }
        }
        assert(found);
        std::cout << "  ✓ AA vs KK win rate is in expected range (78-84%)\n\n";
    }
    
    // Test 2: Flush draw scenario
    {
        std::cout << "Test 2: Flush draw on flop\n";
        HeroHand flush_draw = parseHeroHand("AsKs");
        Board flop = parseBoard({"Qs", "7s", "2d"}); // Two spades on flop
        
        auto result = EquityCalculator::calculateEquity(flush_draw, flop);
        
        std::cout << "  AsKs on Qs7s2d:\n";
        std::cout << "  Overall equity: " << (result.overallWinRate * 100) << "%\n";
        std::cout << "  Number of opponent hands: " << result.numPossibleOpponents << "\n";
        
        // Should have reasonable equity with flush draw + overcards
        assert(result.overallWinRate > 0.55 && result.overallWinRate < 0.75);
        std::cout << "  ✓ Flush draw equity is reasonable\n\n";
    }
    
    // Test 3: Set vs overpair
    {
        std::cout << "Test 3: Set scenario\n";
        HeroHand set = parseHeroHand("7h7d");
        Board flop = parseBoard({"7s", "Kc", "2d"});
        
        auto result = EquityCalculator::calculateEquity(set, flop);
        
        std::cout << "  77 on 7sKc2d (set of sevens):\n";
        std::cout << "  Overall equity: " << (result.overallWinRate * 100) << "%\n";
        
        // Set should be very strong
        assert(result.overallWinRate > 0.85);
        std::cout << "  ✓ Set has strong equity as expected\n\n";
    }
    
    // Test 4: Straight draw
    {
        std::cout << "Test 4: Open-ended straight draw\n";
        HeroHand oesd = parseHeroHand("9h8h");
        Board flop = parseBoard({"7d", "6c", "2s"});
        
        auto result = EquityCalculator::calculateEquity(oesd, flop);
        
        std::cout << "  98 on 762 (open-ended straight draw):\n";
        std::cout << "  Overall equity: " << (result.overallWinRate * 100) << "%\n";
        std::cout << "  ✓ OESD equity calculated\n\n";
    }
    
    std::cout << "All correctness tests passed!\n";
}

// Run performance benchmarks
void runBenchmarks() {
    std::cout << "\n=== PERFORMANCE BENCHMARKS ===\n\n";
    
    std::vector<BenchmarkResult> results;
    
    // Benchmark 1: Preflop (most expensive - 50x49x48x47x46/5! runouts per opponent)
    {
        std::cout << "Benchmark 1: Preflop calculations\n";
        std::cout << "Warning: Preflop calculations are expensive and may take several seconds each.\n\n";
        
        HeroHand aa = parseHeroHand("AsAh");
        Board empty = parseBoard({});
        std::cout << "Running AA preflop...\n";
        results.push_back(benchmarkScenario("AA preflop", aa, empty, true));
        
        HeroHand ak = parseHeroHand("AcKc");
        std::cout << "Running AKs preflop...\n";
        results.push_back(benchmarkScenario("AKs preflop", ak, empty, true));
        
        HeroHand medium = parseHeroHand("8s7s");
        std::cout << "Running 87s preflop...\n";
        results.push_back(benchmarkScenario("87s preflop", medium, empty, true));
    }
    
    // Benchmark 2: Flop (47x46 runouts per opponent)
    {
        std::cout << "\nBenchmark 2: Flop calculations\n";
        HeroHand aa = parseHeroHand("AsAh");
        Board dry_flop = parseBoard({"Kd", "7c", "2s"});
        results.push_back(benchmarkScenario("AA on dry flop", aa, dry_flop));
        
        Board wet_flop = parseBoard({"Qs", "Js", "Ts"});
        results.push_back(benchmarkScenario("AA on wet flop", aa, wet_flop));
        
        HeroHand draw = parseHeroHand("AsKs");
        Board draw_flop = parseBoard({"Qs", "7s", "2d"});
        results.push_back(benchmarkScenario("Flush draw on flop", draw, draw_flop));
    }
    
    // Benchmark 3: Turn (46 runouts per opponent)
    {
        std::cout << "\nBenchmark 3: Turn calculations\n";
        HeroHand aa = parseHeroHand("AsAh");
        Board turn = parseBoard({"Kd", "7c", "2s", "4h"});
        results.push_back(benchmarkScenario("AA on turn", aa, turn));
        
        HeroHand draw = parseHeroHand("AsKs");
        Board draw_turn = parseBoard({"Qs", "7s", "2d", "3h"});
        results.push_back(benchmarkScenario("Flush draw on turn", draw, draw_turn));
    }
    
    // Benchmark 4: River (1 evaluation per opponent - fastest)
    {
        std::cout << "\nBenchmark 4: River calculations\n";
        HeroHand aa = parseHeroHand("AsAh");
        Board river = parseBoard({"Kd", "7c", "2s", "4h", "9d"});
        results.push_back(benchmarkScenario("AA on river", aa, river));
        
        HeroHand missed = parseHeroHand("AsKs");
        Board missed_river = parseBoard({"Qs", "7s", "2d", "3h", "9c"});
        results.push_back(benchmarkScenario("Missed draw on river", missed, missed_river));
    }
    
    // Print summary table
    std::cout << "\n=== BENCHMARK SUMMARY ===\n\n";
    std::cout << std::left << std::setw(25) << "Scenario" 
              << std::right << std::setw(12) << "Time (ms)"
              << std::setw(12) << "Opponents"
              << std::setw(15) << "Evaluations"
              << std::setw(15) << "Eval/sec" << "\n";
    std::cout << std::string(79, '-') << "\n";
    
    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.scenario
                  << std::right << std::setw(12) << std::fixed << std::setprecision(2) << r.milliseconds
                  << std::setw(12) << r.opponentHands
                  << std::setw(15) << r.totalEvaluations
                  << std::setw(15) << std::scientific << std::setprecision(2) << r.evalPerSecond << "\n";
    }
    
    // Calculate totals
    double total_ms = 0;
    uint64_t total_evals = 0;
    for (const auto& r : results) {
        total_ms += r.milliseconds;
        total_evals += r.totalEvaluations;
    }
    
    std::cout << std::string(79, '-') << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_ms << " ms\n";
    std::cout << "Total evaluations: " << total_evals << "\n";
    std::cout << "Average speed: " << std::scientific << std::setprecision(2) 
              << (total_evals / total_ms * 1000.0) << " evals/sec\n";
}

// Interactive test mode
void runInteractive() {
    std::cout << "\n=== INTERACTIVE MODE ===\n";
    std::cout << "Enter hands in format: HeroHand Board\n";
    std::cout << "Example: AsAh Kd7c2s\n";
    std::cout << "Or just: AsAh (for preflop)\n";
    std::cout << "Type 'quit' to exit\n\n";
    
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
        
        std::istringstream iss(line);
        std::string heroStr;
        std::vector<std::string> boardCards;
        
        if (!(iss >> heroStr)) continue;
        
        // Parse board cards (2 chars each)
        std::string boardStr;
        if (iss >> boardStr) {
            for (size_t i = 0; i < boardStr.length(); i += 2) {
                if (i + 1 < boardStr.length()) {
                    boardCards.push_back(boardStr.substr(i, 2));
                }
            }
        }
        
        try {
            HeroHand hero = parseHeroHand(heroStr);
            Board board = parseBoard(boardCards);
            
            std::cout << "\nCalculating equity for " << heroStr;
            if (!boardCards.empty()) {
                std::cout << " on";
                for (const auto& card : boardCards) {
                    std::cout << " " << card;
                }
            }
            std::cout << "...\n";
            
            auto start = std::chrono::high_resolution_clock::now();
            auto result = EquityCalculator::calculateEquity(hero, board, true);
            auto end = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            
            printEquityResult(result);
            std::cout << "\nCalculation time: " << std::fixed << std::setprecision(2) << ms << " ms\n";
            std::cout << "Speed: " << std::scientific << std::setprecision(2) 
                      << (result.totalScenarios / ms * 1000.0) << " evals/sec\n";
            
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
        
        std::cout << "\nEnter next hand (or 'quit'): ";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Poker Equity Calculator Test Suite\n";
    std::cout << "==================================\n";
    
    if (argc > 1) {
        std::string mode(argv[1]);
        if (mode == "test") {
            runCorrectnessTests();
        } else if (mode == "bench") {
            runBenchmarks();
        } else if (mode == "interactive") {
            runInteractive();
        } else {
            std::cout << "Usage: " << argv[0] << " [test|bench|interactive]\n";
            return 1;
        }
    } else {
        // Run all tests by default
        runCorrectnessTests();
        runBenchmarks();
        std::cout << "\nRun with 'interactive' argument for interactive mode\n";
    }
    
    return 0;
}