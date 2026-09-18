
// equity_calculator.cpp
#include "equity.hpp"

HeroHand EquityCalculator::makeHeroHand(int rank1, int suit1, int rank2, int suit2) {
    HeroHand hero;
    
    // Sort by rank (higher rank first)
    if (rank1 > rank2 || (rank1 == rank2 && suit1 < suit2)) {
        hero.rank1 = rank1;
        hero.suit1 = suit1;
        hero.rank2 = rank2;
        hero.suit2 = suit2;
    } else {
        hero.rank1 = rank2;
        hero.suit1 = suit2;
        hero.rank2 = rank1;
        hero.suit2 = suit1;
    }
    
    hero.isPair = (hero.rank1 == hero.rank2);
    return hero;
}

Board EquityCalculator::makeBoard(const std::vector<std::pair<int,int>>& cards) {
    Board board;
    board.numCards = cards.size();
    board.cards = cards;
    
    for (const auto& [suit, rank] : cards) {
        addCard(board.hand, suit, rank);
    }
    
    return board;
}

// Optimized river evaluation (board is complete)
void EquityCalculator::evaluateRiver(const Hand& heroBase, const Hand& oppBase, HandMatchup& matchup) {
    uint32_t heroScore = evaluate_u32(heroBase);
    uint32_t oppScore = evaluate_u32(oppBase);
    
    if (heroScore > oppScore) {
        matchup.wins++;
    } else if (heroScore == oppScore) {
        matchup.ties++;
    }
    matchup.total++;
}

// Optimized turn evaluation (need 1 more card)
void EquityCalculator::evaluateTurn(const Hand& heroBase, const Hand& oppBase, const Hand& usedCards, HandMatchup& matchup) {
    // Pre-calculate available cards
    std::vector<std::pair<int,int>> available;
    available.reserve(52);
    
    for (int s = 0; s < 4; ++s) {
        for (int r = 0; r < 13; ++r) {
            if (!cardConflicts(usedCards, s, r)) {
                available.push_back({s, r});
            }
        }
    }
    
    // Evaluate each river card
    for (const auto& [s, r] : available) {
        Hand heroFinal = heroBase;
        Hand oppFinal = oppBase;
        addCard(heroFinal, s, r);
        addCard(oppFinal, s, r);
        
        uint32_t heroScore = evaluate_u32(heroFinal);
        uint32_t oppScore = evaluate_u32(oppFinal);
        
        if (heroScore > oppScore) matchup.wins++;
        else if (heroScore == oppScore) matchup.ties++;
        matchup.total++;
    }
}

// Optimized flop evaluation (need 2 more cards)
void EquityCalculator::evaluateFlop(const Hand& heroBase, const Hand& oppBase, const Hand& usedCards, HandMatchup& matchup) {
    // Pre-calculate available cards
    std::vector<std::pair<int,int>> available;
    available.reserve(52);
    
    for (int s = 0; s < 4; ++s) {
        for (int r = 0; r < 13; ++r) {
            if (!cardConflicts(usedCards, s, r)) {
                available.push_back({s, r});
            }
        }
    }
    
    int n = available.size();
    
    // Evaluate each turn-river combination
    for (int i = 0; i < n; ++i) {
        const auto& [s1, r1] = available[i];
        
        for (int j = i + 1; j < n; ++j) {
            const auto& [s2, r2] = available[j];
            
            Hand heroFinal = heroBase;
            Hand oppFinal = oppBase;
            addCard(heroFinal, s1, r1);
            addCard(heroFinal, s2, r2);
            addCard(oppFinal, s1, r1);
            addCard(oppFinal, s2, r2);
            
            uint32_t heroScore = evaluate_u32(heroFinal);
            uint32_t oppScore = evaluate_u32(oppFinal);
            
            if (heroScore > oppScore) matchup.wins++;
            else if (heroScore == oppScore) matchup.ties++;
            matchup.total++;
        }
    }
}

// Optimized preflop evaluation (need 5 cards)
void EquityCalculator::evaluatePreflop(const Hand& heroBase, const Hand& oppBase, const Hand& usedCards, HandMatchup& matchup) {
    // Pre-calculate available cards
    std::vector<int> available;
    available.reserve(52);
    
    // Pack suit and rank into single int for faster access
    for (int s = 0; s < 4; ++s) {
        for (int r = 0; r < 13; ++r) {
            if (!cardConflicts(usedCards, s, r)) {
                available.push_back((s << 4) | r);
            }
        }
    }
    
    int n = available.size();
    
    // Unrolled 5-card combination generation with inline evaluation
    for (int i1 = 0; i1 < n - 4; ++i1) {
        int c1 = available[i1];
        int s1 = c1 >> 4, r1 = c1 & 0xF;
        
        for (int i2 = i1 + 1; i2 < n - 3; ++i2) {
            int c2 = available[i2];
            int s2 = c2 >> 4, r2 = c2 & 0xF;
            
            for (int i3 = i2 + 1; i3 < n - 2; ++i3) {
                int c3 = available[i3];
                int s3 = c3 >> 4, r3 = c3 & 0xF;
                
                for (int i4 = i3 + 1; i4 < n - 1; ++i4) {
                    int c4 = available[i4];
                    int s4 = c4 >> 4, r4 = c4 & 0xF;
                    
                    // Inner loop unrolled for better performance
                    Hand heroPartial = heroBase;
                    Hand oppPartial = oppBase;
                    heroPartial[s1] |= (1u << r1);
                    heroPartial[s2] |= (1u << r2);
                    heroPartial[s3] |= (1u << r3);
                    heroPartial[s4] |= (1u << r4);
                    oppPartial[s1] |= (1u << r1);
                    oppPartial[s2] |= (1u << r2);
                    oppPartial[s3] |= (1u << r3);
                    oppPartial[s4] |= (1u << r4);
                    
                    for (int i5 = i4 + 1; i5 < n; ++i5) {
                        int c5 = available[i5];
                        int s5 = c5 >> 4, r5 = c5 & 0xF;
                        
                        Hand heroFinal = heroPartial;
                        Hand oppFinal = oppPartial;
                        heroFinal[s5] |= (1u << r5);
                        oppFinal[s5] |= (1u << r5);
                        
                        uint32_t heroScore = evaluate_u32(heroFinal);
                        uint32_t oppScore = evaluate_u32(oppFinal);
                        
                        if (heroScore > oppScore) matchup.wins++;
                        else if (heroScore == oppScore) matchup.ties++;
                        matchup.total++;
                    }
                }
            }
        }
    }
}

EquityResult EquityCalculator::calculateEquity(const HeroHand& hero, const Board& board, bool showProgress) {
    if (showProgress) {
        auto callback = [](int current, int total, double elapsed_ms) {
            // Simple progress bar
            int barWidth = 50;
            float progress = static_cast<float>(current) / total;
            int pos = barWidth * progress;
            
            std::cout << "\r[";
            for (int i = 0; i < barWidth; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << int(progress * 100.0) << "% ";
            std::cout << "(" << current << "/" << total << " hands) ";
            
            // Estimate time remaining
            if (current > 0 && elapsed_ms > 0) {
                double rate = current / elapsed_ms; // hands per ms
                double remaining_ms = (total - current) / rate;
                if (remaining_ms > 1000) {
                    std::cout << "ETA: " << std::fixed << std::setprecision(1) 
                             << (remaining_ms / 1000.0) << "s ";
                } else {
                    std::cout << "ETA: " << std::fixed << std::setprecision(0) 
                             << remaining_ms << "ms ";
                }
            }
            std::cout << std::flush;
        };
        return calculateEquityWithCallback(hero, board, callback);
    }
    return calculateEquityWithCallback(hero, board, nullptr);
}

EquityResult EquityCalculator::calculateEquityWithCallback(const HeroHand& hero, const Board& board, 
                                                           ProgressCallback callback) {
    EquityResult result;
    result.totalScenarios = 0;
    result.totalWins = 0;
    result.totalTies = 0;
    
    // Create hand with all used cards (hero + board)
    Hand usedCards = board.hand;
    addCard(usedCards, hero.suit1, hero.rank1);
    addCard(usedCards, hero.suit2, hero.rank2);
    
    // Create base hands for hero and board
    Hand heroBase = board.hand;
    addCard(heroBase, hero.suit1, hero.rank1);
    addCard(heroBase, hero.suit2, hero.rank2);   
    // First, count total possible opponent hands for progress tracking
    int totalOpponentHands = 0;
    for (int suit1 = 0; suit1 < 4; ++suit1) {
        for (int rank1 = 0; rank1 < 13; ++rank1) {
            if (cardConflicts(usedCards, suit1, rank1)) continue;
            for (int suit2 = 0; suit2 < 4; ++suit2) {
                for (int rank2 = 0; rank2 < 13; ++rank2) {
                    if (cardConflicts(usedCards, suit2, rank2)) continue;
                    if (suit1 == suit2 && rank1 == rank2) continue;
                    if (suit1 > suit2 || (suit1 == suit2 && rank1 > rank2)) continue;
                    totalOpponentHands++;
                }
            }
        }
    }
    
    int handsProcessed = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    int lastUpdate = 0;
    
    // Iterate through all possible opponent hands
    for (int suit1 = 0; suit1 < 4; ++suit1) {
        for (int rank1 = 0; rank1 < 13; ++rank1) {
            if (cardConflicts(usedCards, suit1, rank1)) continue;
            
            for (int suit2 = 0; suit2 < 4; ++suit2) {
                for (int rank2 = 0; rank2 < 13; ++rank2) {
                    if (cardConflicts(usedCards, suit2, rank2)) continue;
                    if (suit1 == suit2 && rank1 == rank2) continue;
                    
                    // To avoid counting same hand twice
                    if (suit1 > suit2 || (suit1 == suit2 && rank1 > rank2)) continue;
                    
                    // Update progress
                    handsProcessed++;
                    if (callback && (handsProcessed == 1 || handsProcessed == totalOpponentHands || 
                        handsProcessed - lastUpdate >= std::max(1, totalOpponentHands / 100))) {
                        auto currentTime = std::chrono::high_resolution_clock::now();
                        double elapsed = std::chrono::duration<double, std::milli>(currentTime - startTime).count();
                        callback(handsProcessed, totalOpponentHands, elapsed);
                        lastUpdate = handsProcessed;
                    }
                    
                    // Create opponent's base hand
                    Hand oppBase = board.hand;
                    addCard(oppBase, suit1, rank1);
                    addCard(oppBase, suit2, rank2);
                    
                    // Update used cards to include opponent's cards
                    Hand totalUsed = usedCards;
                    addCard(totalUsed, suit1, rank1);
                    addCard(totalUsed, suit2, rank2);
                    
                    // Calculate matchup
                    HandMatchup matchup;
                    matchup.opp_rank1 = rank1;
                    matchup.opp_rank2 = rank2;
                    matchup.opp_suit1 = suit1;
                    matchup.opp_suit2 = suit2;
                    
                    // Use optimized evaluation based on street
                    switch (board.numCards) {
                        case 5:
                            evaluateRiver(heroBase, oppBase, matchup);
                            break;
                        case 4:
                            evaluateTurn(heroBase, oppBase, totalUsed, matchup);
                            break;
                        case 3:
                            evaluateFlop(heroBase, oppBase, totalUsed, matchup);
                            break;
                        case 0:
                            evaluatePreflop(heroBase, oppBase, totalUsed, matchup);
                            break;
                        default:
                            // Handle 1-2 card boards if needed
                            break;
                    }
                    
                    // Calculate rates
                    if (matchup.total > 0) {
                        matchup.winRate = static_cast<double>(matchup.wins) / matchup.total;
                        matchup.tieRate = static_cast<double>(matchup.ties) / matchup.total;
                    }
                    
                    result.matchups.push_back(matchup);
                    result.totalWins += matchup.wins;
                    result.totalTies += matchup.ties;
                    result.totalScenarios += matchup.total;
                }
            }
        }
    }
    
    // Clear progress bar line
    if (callback) {
        std::cout << "\r" << std::string(100, ' ') << "\r" << std::flush;
    }
    
    // Calculate overall statistics
    result.numPossibleOpponents = result.matchups.size();
    
    if (result.totalScenarios > 0) {
        result.overallWinRate = static_cast<double>(result.totalWins) / result.totalScenarios;
        result.overallTieRate = static_cast<double>(result.totalTies) / result.totalScenarios;
    }
    
    if (!result.matchups.empty()) {
        result.avgWinRate = 0;
        result.minWinRate = 1.0;
        result.maxWinRate = 0.0;
        
        for (const auto& m : result.matchups) {
            result.avgWinRate += m.winRate;
            result.minWinRate = std::min(result.minWinRate, m.winRate);
            result.maxWinRate = std::max(result.maxWinRate, m.winRate);
        }
        result.avgWinRate /= result.matchups.size();
    }
    
    return result;
}