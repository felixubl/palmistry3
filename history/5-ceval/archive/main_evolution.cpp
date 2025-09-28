#include "evaluator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

// --------------------------- Units & constants -------------------------------
static constexpr int BIG_BLIND_CHIPS   = 2;
static constexpr int SMALL_BLIND_CHIPS = 1;
static constexpr int STARTING_STACK_BB = 100;
static constexpr int STARTING_STACK_CHIPS = STARTING_STACK_BB * BIG_BLIND_CHIPS;

static inline double chips_to_bb(int chips) {
    return double(chips) / double(BIG_BLIND_CHIPS);
}

// --------------------------- Game state and actions ---------------------------
enum class Action { FOLD, CHECK_CALL, RAISE };
enum class Street { PREFLOP, FLOP, TURN, RIVER, SHOWDOWN };

// Raise bucket indices (engine & bot agree on this order)
static constexpr double RAISE_BUCKET_PCTS[] = {0.25, 0.33, 0.50, 0.66, 1.00, 1.25, 2.50};
static constexpr int NUM_RAISE_BUCKETS = 7;
// Model output layout: 0=Fold/Check, 1=Call/Check, 2..(2+NUM_RAISE_BUCKETS-1)=bucketed raises, last=All-in
static constexpr int OUTPUTS = 2 + NUM_RAISE_BUCKETS + 1;

struct GameState {
    std::array<uint16_t, 2> hole_cards;  // 0..51
    int position;                        // 0 = button (SB), 1 = BB

    int pot_size_chips;
    int stack_size_chips;
    int to_call_chips;

    bool can_fold;
    bool can_check_call;
    bool can_raise;
    int min_raise_chips;                 // minimum *raise amount* over call (info)
    int max_raise_chips;                 // maximum *raise amount* over call (all-in) (info)

    Street street;                       // current betting street
};

struct ActionDecision {
    Action action;
    int param; // if action==RAISE: bucket index 0..(NUM_RAISE_BUCKETS-1), or NUM_RAISE_BUCKETS for All-in
};

// --------------------------- Stats helper (robust) ---------------------------
struct GenStats {
    double best = std::numeric_limits<double>::quiet_NaN();
    double median = std::numeric_limits<double>::quiet_NaN();
    double worst = std::numeric_limits<double>::quiet_NaN();
    double mean = std::numeric_limits<double>::quiet_NaN();
    std::size_t n = 0;
};

static inline bool is_finite(double x) { return std::isfinite(x); }

static GenStats compute_stats_from(const std::vector<double>& raw) {
    GenStats s{};
    std::vector<double> v; v.reserve(raw.size());
    for (double x : raw) if (is_finite(x)) v.push_back(x);
    s.n = v.size(); if (!s.n) return s;
    auto [mn, mx] = std::minmax_element(v.begin(), v.end());
    s.worst = *mn; s.best = *mx;
    s.mean  = std::accumulate(v.begin(), v.end(), 0.0) / double(s.n);

    std::vector<double> tmp = v;
    auto kth = [&](std::size_t k){ std::nth_element(tmp.begin(), tmp.begin()+k, tmp.end()); return tmp[k]; };
    if (s.n % 2) s.median = kth(s.n/2);
    else { double a = kth(s.n/2-1); tmp = v; double b = kth(s.n/2); s.median = 0.5*(a+b); }
    return s;
}
static GenStats compute_stats_top_k(const std::vector<double>& raw, std::size_t k) {
    if (raw.empty()) return {};
    k = std::min(k, raw.size());
    std::vector<double> v = raw;
    std::nth_element(v.begin(), v.begin()+k, v.end(), std::greater<double>());
    v.resize(k);
    return compute_stats_from(v);
}

// --------------------------- Poker bot interface -----------------------------
class PokerBot {
public:
    virtual ~PokerBot() = default;
    virtual ActionDecision decide_action(const GameState& state, XorShift64& rng) = 0;
    virtual std::unique_ptr<PokerBot> clone() const = 0;
    virtual void mutate(XorShift64& rng, double mutation_rate = 0.1) = 0;

    double total_bb_won = 0.0;
    int hands_played = 0;
    double avg_bb_per_hand() const { return hands_played > 0 ? total_bb_won / hands_played : 0.0; }
};

// --------------------------- Feed-forward bot (bucketed) ---------------------
// Input features:
//
// 52 card one-hots +
// position, pot, stack, to_call (4)
// can_fold, can_check_call, can_raise (3)
// min_raise, max_raise (2)
// street one-hot (4)
//
// total = 65
static constexpr int INPUT_FEATURES = 65;

class FeedForwardBot : public PokerBot {
private:
    struct Layer {
        std::vector<std::vector<double>> weights;
        std::vector<double> biases;
        Layer(int inputs, int outputs, XorShift64& rng)
            : weights(outputs, std::vector<double>(inputs)), biases(outputs) {
            double scale = std::sqrt(2.0 / (inputs + outputs));
            for (auto& row : weights) for (auto& w : row)
                w = scale * (2.0 * (rng.next() / double(UINT64_MAX)) - 1.0);
            for (auto& b : biases)
                b = scale * (2.0 * (rng.next() / double(UINT64_MAX)) - 1.0);
        }
    };

    // 3 layers: INPUT_FEATURES -> 128 -> 64 -> OUTPUTS
    std::vector<Layer> layers;

public:
    explicit FeedForwardBot(const std::vector<int>& layer_sizes, XorShift64& rng) {
        for (std::size_t i = 0; i + 1 < layer_sizes.size(); ++i)
            layers.emplace_back(layer_sizes[i], layer_sizes[i + 1], rng);
    }
    FeedForwardBot(const FeedForwardBot& o) : layers(o.layers) {}

    std::vector<double> encode_state(const GameState& s) const {
        std::vector<double> features;
        std::vector<double> cards(52, 0.0);
        cards[s.hole_cards[0]] = 1.0;
        cards[s.hole_cards[1]] = 1.0;
        features.insert(features.end(), cards.begin(), cards.end());
        features.push_back(double(s.position));
        features.push_back(s.pot_size_chips / 200.0);
        features.push_back(s.stack_size_chips / 200.0);
        features.push_back(s.to_call_chips / 200.0);
        features.push_back(s.can_fold ? 1.0 : 0.0);
        features.push_back(s.can_check_call ? 1.0 : 0.0);
        features.push_back(s.can_raise ? 1.0 : 0.0);
        features.push_back(s.min_raise_chips / 200.0);
        features.push_back(s.max_raise_chips / 200.0);
        // Street one-hot
        features.push_back(s.street == Street::PREFLOP ? 1.0 : 0.0);
        features.push_back(s.street == Street::FLOP    ? 1.0 : 0.0);
        features.push_back(s.street == Street::TURN    ? 1.0 : 0.0);
        features.push_back(s.street == Street::RIVER   ? 1.0 : 0.0);
        return features;
    }

    // ---- Forward helpers ----
    static inline void relu_layer(const Layer& L, const std::vector<double>& x, std::vector<double>& y) {
        y.assign(L.biases.begin(), L.biases.end());
        for (std::size_t i=0;i<L.weights.size();++i){
            const auto& w = L.weights[i];
            const std::size_t cols = std::min(w.size(), x.size());
            double s = L.biases[i];
            for (std::size_t j=0;j<cols;++j) s += w[j]*x[j];
            y[i] = std::max(0.0, s);
        }
    }
    static inline void linear_layer(const Layer& L, const std::vector<double>& x, std::vector<double>& y) {
        y.assign(L.biases.begin(), L.biases.end());
        for (std::size_t i=0;i<L.weights.size();++i){
            const auto& w = L.weights[i];
            const std::size_t cols = std::min(w.size(), x.size());
            double s = L.biases[i];
            for (std::size_t j=0;j<cols;++j) s += w[j]*x[j];
            y[i] = s;
        }
    }

    // For gameplay: sample an action
    ActionDecision decide_action(const GameState& state, XorShift64& rng) override {
        auto x = encode_state(state);
        std::vector<double> h1, h2, logits;
        relu_layer(layers[0], x, h1);
        relu_layer(layers[1], h1, h2);
        linear_layer(layers[2], h2, logits);

        // Mask invalid choices
        std::vector<double> allowed_logits;
        std::vector<int>    allowed_ids;
        allowed_logits.reserve(OUTPUTS); allowed_ids.reserve(OUTPUTS);

        if (state.to_call_chips > 0) { // fold available only facing a bet
            allowed_ids.push_back(0); allowed_logits.push_back(logits[0]);
        }
        allowed_ids.push_back(1); allowed_logits.push_back(logits[1]); // call/check

        if (state.can_raise) {
            for (int b = 0; b < NUM_RAISE_BUCKETS; ++b) {
                allowed_ids.push_back(2 + b);
                allowed_logits.push_back(logits[2 + b]);
            }
            allowed_ids.push_back(2 + NUM_RAISE_BUCKETS); // all-in
            allowed_logits.push_back(logits[2 + NUM_RAISE_BUCKETS]);
        }

        // Softmax sample
        double maxl = *std::max_element(allowed_logits.begin(), allowed_logits.end());
        double Z=0.0; for (double& v: allowed_logits){ v = std::exp(v - maxl); Z += v; }
        double r = (rng.next() / double(UINT64_MAX)) * Z;
        int pick = 0;
        for (int i=0;i<(int)allowed_logits.size();++i){ r -= allowed_logits[i]; if (r <= 0){ pick = i; break; } }

        int id = allowed_ids[pick];
        if (id == 0) return {Action::FOLD, 0};
        if (id == 1) return {Action::CHECK_CALL, 0};
        return {Action::RAISE, id - 2}; // 0..NUM_RAISE_BUCKETS-1, or NUM_RAISE_BUCKETS=All-in
    }

    // For analysis: get probability vector over OUTPUTS (zeros for disallowed)
    std::vector<double> policy_probs(const GameState& state) const {
        auto x = encode_state(state);
        std::vector<double> h1, h2, logits;
        relu_layer(layers[0], x, h1);
        relu_layer(layers[1], h1, h2);
        linear_layer(layers[2], h2, logits);

        std::vector<double> allowed_logits;
        std::vector<int>    allowed_ids;
        if (state.to_call_chips > 0) { allowed_ids.push_back(0); allowed_logits.push_back(logits[0]); } // fold
        allowed_ids.push_back(1); allowed_logits.push_back(logits[1]); // call/check
        if (state.can_raise) {
            for (int b = 0; b < NUM_RAISE_BUCKETS; ++b){
                allowed_ids.push_back(2 + b);
                allowed_logits.push_back(logits[2 + b]);
            }
            allowed_ids.push_back(2 + NUM_RAISE_BUCKETS);
            allowed_logits.push_back(logits[2 + NUM_RAISE_BUCKETS]);
        }

        double maxl = *std::max_element(allowed_logits.begin(), allowed_logits.end());
        double Z=0.0; for (double& v: allowed_logits){ v = std::exp(v - maxl); Z += v; }
        for (double& v: allowed_logits) v /= (Z > 0.0 ? Z : 1.0);

        std::vector<double> probs(OUTPUTS, 0.0);
        for (std::size_t i=0;i<allowed_ids.size();++i) probs[allowed_ids[i]] = allowed_logits[i];
        return probs;
    }

    std::unique_ptr<PokerBot> clone() const override { return std::make_unique<FeedForwardBot>(*this); }

    void mutate(XorShift64& rng, double mutation_rate = 0.1) override {
        for (auto& L : layers) {
            for (auto& row : L.weights) for (auto& w : row)
                if (rng.next() / double(UINT64_MAX) < mutation_rate)
                    w += 0.1 * (2.0 * (rng.next() / double(UINT64_MAX)) - 1.0);
            for (auto& b : L.biases)
                if (rng.next() / double(UINT64_MAX) < mutation_rate)
                    b += 0.1 * (2.0 * (rng.next() / double(UINT64_MAX)) - 1.0);
        }
    }
};

// --------------------------- Game engine -------------------------------------
class HeadsUpGame {
private:
    XorShift64& rng;

    struct PlayerState {
        int stack;           // chips remaining
        int bet_this_round;  // chips committed this street
        int contributed;     // chips contributed overall
        uint16_t hole_cards[2];
        bool folded;
        bool all_in;
        PlayerState()
            : stack(STARTING_STACK_CHIPS), bet_this_round(0), contributed(0),
              folded(false), all_in(false) {}
    };

    struct RoundResult { bool folded = false; int folder = -1; };

    static inline const char* street_name(Street st){
        switch (st){
            case Street::PREFLOP: return "Preflop";
            case Street::FLOP:    return "Flop";
            case Street::TURN:    return "Turn";
            case Street::RIVER:   return "River";
            default:              return "Showdown";
        }
    }

    // Compute desired "bet_to" (the actor's bet_this_round after action) from bucket.
    static int bucket_target_bet_to(int current_bet,
                                    int highest_bet,
                                    int to_call,
                                    int pot,
                                    int stack,
                                    int last_raise_sz,
                                    int bucket_idx) {
        // All-in bucket
        if (bucket_idx == NUM_RAISE_BUCKETS) {
            return current_bet + stack; // shove
        }

        double pct = RAISE_BUCKET_PCTS[bucket_idx]; // e.g., 0.50 for 50% pot
        long long desired;

        if (to_call == 0) {
            // Bet = pct * pot
            desired = (long long)std::llround(pct * pot);
            desired = std::max<long long>(desired, BIG_BLIND_CHIPS); // avoid dust
            desired = current_bet + std::min<long long>(desired, stack);
            // If there was a previous raise on this street, enforce min-raise step
            long long min_to = (highest_bet == 0) ? desired : (long long)highest_bet + last_raise_sz;
            desired = std::max<long long>(desired, min_to);
        } else {
            // Raise-to ≈ highest_bet + to_call + pct*(pot + to_call)
            long long size_part = (long long)std::llround(pct * (pot + to_call));
            long long raise_to  = (long long)highest_bet + to_call + size_part;
            long long min_to    = (long long)highest_bet + to_call + last_raise_sz; // NLHE min raise
            desired = std::max<long long>(raise_to, min_to);
            desired = std::min<long long>(desired, (long long)current_bet + stack);
        }

        return (int)desired;
    }

    RoundResult betting_round(PlayerState P[2],
                              int& pot,
                              int button,
                              Street street,
                              PokerBot& bot0,
                              PokerBot& bot1,
                              std::ostringstream* log = nullptr) {
        int current_player = (street == Street::PREFLOP) ? button : (1 - button);
        int last_raiser    = -1;
        int highest_bet    = std::max(P[0].bet_this_round, P[1].bet_this_round);
        int last_raise_sz  = BIG_BLIND_CHIPS; // min-raise starts at 1BB

        auto log_prefix = [&](int pid){
            if (!log) return;
            *log << "[" << street_name(street) << "] P" << pid << " ";
        };
        auto bucket_name = [&](int bucket)->std::string{
            if (bucket == NUM_RAISE_BUCKETS) return "All-in";
            std::ostringstream os; os << int(std::round(100*RAISE_BUCKET_PCTS[bucket])) << "% pot";
            return os.str();
        };

        bool someone_acted = false;

        while (true) {
            if (P[0].folded || P[1].folded) break;
            if (P[current_player].all_in) {
                if (P[0].bet_this_round == P[1].bet_this_round) break;
            }

            int opp = 1 - current_player;
            int to_call = P[opp].bet_this_round - P[current_player].bet_this_round;
            bool can_raise = P[current_player].stack > to_call;

            GameState st{};
            st.hole_cards = {P[current_player].hole_cards[0], P[current_player].hole_cards[1]};
            st.position = (current_player == button) ? 0 : 1;
            st.pot_size_chips   = pot;
            st.stack_size_chips = P[current_player].stack;
            st.to_call_chips    = to_call;
            st.can_fold = to_call > 0;
            st.can_check_call = true;
            st.can_raise = can_raise;
            st.max_raise_chips = std::max(0, P[current_player].stack - to_call);
            st.min_raise_chips = std::min(last_raise_sz, st.max_raise_chips);
            st.street = street;

            PokerBot& bot = (current_player == 0) ? bot0 : bot1;
            ActionDecision d = bot.decide_action(st, rng);

            if (d.action == Action::FOLD && st.can_fold) {
                P[current_player].folded = true;
                if (log) { log_prefix(current_player); *log << "folds\n"; }
                return {true, current_player};
            }

            if (d.action == Action::CHECK_CALL || !st.can_raise) {
                if (to_call == 0) {
                    if (log) { log_prefix(current_player); *log << "checks\n"; }
                    if (someone_acted && last_raiser == -1) break; // check-check
                } else {
                    int call_amount = std::min(std::max(0, to_call), P[current_player].stack);
                    P[current_player].bet_this_round += call_amount;
                    P[current_player].stack -= call_amount;
                    P[current_player].contributed += call_amount;
                    pot += call_amount;
                    if (log) { log_prefix(current_player); *log << "calls " << call_amount
                                << " chips (" << chips_to_bb(call_amount) << " BB)\n"; }
                    if (P[current_player].stack == 0) P[current_player].all_in = true;
                    if (P[current_player].bet_this_round == P[opp].bet_this_round) break;
                }
            } else { // RAISE via bucket
                int bucket = d.param; // 0..NUM_RAISE_BUCKETS-1 or NUM_RAISE_BUCKETS (All-in)
                int desired_to = bucket_target_bet_to(
                    /*current_bet*/ P[current_player].bet_this_round,
                    /*highest_bet*/ highest_bet,
                    /*to_call*/     to_call,
                    /*pot*/         pot,
                    /*stack*/       P[current_player].stack,
                    /*last_raise*/  last_raise_sz,
                    /*bucket*/      bucket
                );

                int total_put_in = std::max(0, desired_to - P[current_player].bet_this_round);
                total_put_in = std::min(total_put_in, P[current_player].stack);

                int prev_bet = P[current_player].bet_this_round;
                P[current_player].bet_this_round += total_put_in;
                P[current_player].stack          -= total_put_in;
                P[current_player].contributed    += total_put_in;
                pot += total_put_in;

                int new_bet    = P[current_player].bet_this_round;
                int raise_size = new_bet - highest_bet;
                if (raise_size >= last_raise_sz) last_raise_sz = raise_size;
                highest_bet = new_bet;
                last_raiser = current_player;

                if (log) {
                    log_prefix(current_player);
                    *log << "raises (" << bucket_name(bucket) << ") to " << new_bet
                         << " chips (" << chips_to_bb(new_bet) << " BB)"
                         << " [+" << (new_bet - prev_bet - to_call) << " raise, "
                         << chips_to_bb(new_bet - prev_bet - to_call) << " BB]\n";
                }

                if (P[current_player].stack == 0) P[current_player].all_in = true;
            }

            someone_acted = true;
            current_player = 1 - current_player;

            if (last_raiser != -1 && current_player == last_raiser &&
                P[0].bet_this_round == P[1].bet_this_round) {
                break;
            }
        }

        return {false, -1};
    }

public:
    explicit HeadsUpGame(XorShift64& rng) : rng(rng) {}

    // Returns P0 net in BB
    double play_hand(PokerBot& bot0, PokerBot& bot1) {
        return play_hand_impl(bot0, bot1, /*log=*/nullptr);
    }
    std::string play_hand_logged(PokerBot& bot0, PokerBot& bot1) {
        std::ostringstream log; play_hand_impl(bot0, bot1, &log); return log.str();
    }

private:
    double play_hand_impl(PokerBot& bot0, PokerBot& bot1, std::ostringstream* log) {
        auto cs = [](uint16_t c){ static const char* R="23456789TJQKA"; static const char* S="cdhs";
                                  return std::string{R[c%13]} + S[c/13]; };

        PlayerState P[2];
        int pot = 0;
        int button = rng.uniform(2);
        if (log) *log << "Button: P" << button << "\n";

        uint64_t used_cards = 0;

        // Deal hole cards
        for (int p = 0; p < 2; ++p) {
            for (int c = 0; c < 2; ++c) {
                uint32_t card;
                do { card = rng.uniform(52); } while (used_cards & (1ULL << card));
                used_cards |= (1ULL << card);
                P[p].hole_cards[c] = static_cast<uint16_t>(card);
            }
        }
        if (log) {
            *log << "P0 hole: " << cs(P[0].hole_cards[0]) << " " << cs(P[0].hole_cards[1]) << "\n";
            *log << "P1 hole: " << cs(P[1].hole_cards[0]) << " " << cs(P[1].hole_cards[1]) << "\n";
        }

        // Blinds
        int sb_pos = button, bb_pos = 1 - button;
        P[sb_pos].bet_this_round = SMALL_BLIND_CHIPS; P[sb_pos].stack -= SMALL_BLIND_CHIPS; P[sb_pos].contributed += SMALL_BLIND_CHIPS;
        P[bb_pos].bet_this_round = BIG_BLIND_CHIPS;   P[bb_pos].stack -= BIG_BLIND_CHIPS;   P[bb_pos].contributed += BIG_BLIND_CHIPS;
        pot = SMALL_BLIND_CHIPS + BIG_BLIND_CHIPS;
        if (log) {
            *log << "Post blinds: SB=P" << sb_pos << " (" << SMALL_BLIND_CHIPS << " chips, "
                 << chips_to_bb(SMALL_BLIND_CHIPS) << " BB), BB=P" << bb_pos << " (" << BIG_BLIND_CHIPS
                 << " chips, " << chips_to_bb(BIG_BLIND_CHIPS) << " BB)\n";
        }

        // ----------- PREFLOP -----------
        {
            RoundResult rr = betting_round(P, pot, button, Street::PREFLOP, bot0, bot1, log);
            if (rr.folded) {
                int net_p0 = (rr.folder == 0) ? -P[0].contributed : pot - P[0].contributed;
                if (log) {
                    *log << "Board: (no showdown)\n";
                    *log << "Winner: P" << (1 - rr.folder)
                         << " | P0 net = " << net_p0 << " chips (" << std::fixed << std::setprecision(3)
                         << chips_to_bb(net_p0) << " BB)\n";
                }
                return chips_to_bb(net_p0);
            }
        }

        // Deal flop (3)
        std::vector<uint16_t> board;
        for (int i = 0; i < 3; ++i) {
            uint32_t c;
            do { c = rng.uniform(52); } while (used_cards & (1ULL << c));
            used_cards |= (1ULL << c);
            board.push_back(static_cast<uint16_t>(c));
        }
        if (log) { *log << "Flop: "; for (size_t i=0;i<board.size();++i) *log << cs(board[i]) << (i+1==board.size()?"":" ");
                   *log << "\n"; }
        P[0].bet_this_round = 0; P[1].bet_this_round = 0;
        {
            RoundResult rr = betting_round(P, pot, button, Street::FLOP, bot0, bot1, log);
            if (rr.folded) {
                int net_p0 = (rr.folder == 0) ? -P[0].contributed : pot - P[0].contributed;
                if (log) { *log << "Turn: (no turn)\nRiver: (no river)\n";
                           *log << "Winner: P" << (1 - rr.folder)
                                << " | P0 net = " << net_p0 << " chips (" << std::fixed << std::setprecision(3)
                                << chips_to_bb(net_p0) << " BB)\n"; }
                return chips_to_bb(net_p0);
            }
        }

        // Turn
        { uint32_t c; do { c = rng.uniform(52); } while (used_cards & (1ULL << c));
          used_cards |= (1ULL << c); board.push_back(static_cast<uint16_t>(c)); }
        if (log) *log << "Turn: " << cs(board[3]) << "\n";
        P[0].bet_this_round = 0; P[1].bet_this_round = 0;
        {
            RoundResult rr = betting_round(P, pot, button, Street::TURN, bot0, bot1, log);
            if (rr.folded) {
                int net_p0 = (rr.folder == 0) ? -P[0].contributed : pot - P[0].contributed;
                if (log) { *log << "River: (no river)\n";
                           *log << "Winner: P" << (1 - rr.folder)
                                << " | P0 net = " << net_p0 << " chips (" << std::fixed << std::setprecision(3)
                                << chips_to_bb(net_p0) << " BB)\n"; }
                return chips_to_bb(net_p0);
            }
        }

        // River
        { uint32_t c; do { c = rng.uniform(52); } while (used_cards & (1ULL << c));
          used_cards |= (1ULL << c); board.push_back(static_cast<uint16_t>(c)); }
        if (log) *log << "River: " << cs(board[4]) << "\n";
        P[0].bet_this_round = 0; P[1].bet_this_round = 0;
        {
            RoundResult rr = betting_round(P, pot, button, Street::RIVER, bot0, bot1, log);
            if (rr.folded) {
                int net_p0 = (rr.folder == 0) ? -P[0].contributed : pot - P[0].contributed;
                if (log) { *log << "Winner: P" << (1 - rr.folder)
                                << " | P0 net = " << net_p0 << " chips (" << std::fixed << std::setprecision(3)
                                << chips_to_bb(net_p0) << " BB)\n"; }
                return chips_to_bb(net_p0);
            }
        }

        // Showdown
        Hand h0 = empty_hand(), h1 = empty_hand();
        for (int c = 0; c < 2; ++c) {
            int s0 = P[0].hole_cards[c] / 13, r0 = P[0].hole_cards[c] % 13;
            int s1 = P[1].hole_cards[c] / 13, r1 = P[1].hole_cards[c] % 13;
            h0[s0] = static_cast<uint16_t>(h0[s0] | (uint16_t(1) << r0));
            h1[s1] = static_cast<uint16_t>(h1[s1] | (uint16_t(1) << r1));
        }
        for (auto c : board) {
            int s = c / 13, r = c % 13;
            h0[s] = static_cast<uint16_t>(h0[s] | (uint16_t(1) << r));
            h1[s] = static_cast<uint16_t>(h1[s] | (uint16_t(1) << r));
        }
        uint32_t sc0 = evaluate_u32(h0), sc1 = evaluate_u32(h1);
        int winner = (sc0 > sc1) ? 0 : 1;

        int net_p0 = (winner == 0 ? pot - P[0].contributed : -P[0].contributed);
        if (log) {
            *log << "Board: "; for (size_t i=0;i<board.size(); ++i) *log << cs(board[i]) << (i+1==board.size()?"":" ");
            *log << "\n";
            *log << "Winner: P" << winner
                 << " | P0 net = " << net_p0 << " chips (" << std::fixed << std::setprecision(3)
                 << chips_to_bb(net_p0) << " BB)\n";
        }
        return chips_to_bb(net_p0);
    }
};

// --------------------------- Range export helpers ----------------------------
static inline int rank_of(uint16_t c){ return c % 13; }
static inline int suit_of(uint16_t c){ return c / 13; }

// Generate all distinct unordered hole-card combos (1326)
static std::vector<std::array<uint16_t,2>> all_unique_combos() {
    std::vector<std::array<uint16_t,2>> v;
    v.reserve(1326);
    for (uint16_t a=0;a<52;++a)
        for (uint16_t b=a+1;b<52;++b)
            v.push_back({a,b});
    return v;
}

// AKs/AQo/77 key
static std::string hand_key(uint16_t c1, uint16_t c2) {
    int r1 = rank_of(c1), r2 = rank_of(c2);
    int rmax = std::max(r1,r2), rmin = std::min(r1,r2);
    static const char RC[]="23456789TJQKA";
    char a = RC[rmax], b = RC[rmin];
    bool pair = (r1 == r2);
    if (pair) return std::string(1,a) + std::string(1,b);
    bool suited = (suit_of(c1) == suit_of(c2));
    return std::string(1,a) + std::string(1,b) + (suited ? "s":"o");
}

// Write simple CSV list "hand,score"
static void write_range_csv(const std::string& path,
                            const std::map<std::string,double>& agg_by_hand) {
    std::ofstream out(path);
    out << "hand,prob\n";
    for (auto& kv : agg_by_hand) out << kv.first << "," << kv.second << "\n";
    out.close();
}

// Dump preflop **open-raise** probability for a model (SB/Button and BB)
static void dump_preflop_ranges_csv(const PokerBot& bot, const std::string& prefix) {
    const FeedForwardBot* f = dynamic_cast<const FeedForwardBot*>(&bot);
    if (!f) return;

    auto combos = all_unique_combos();
    auto eval_side = [&](int position){ // 0=button(SB), 1=BB
        std::map<std::string,double> acc; std::map<std::string,int> count;
        for (auto cards : combos) {
            GameState st{};
            st.hole_cards = cards;
            st.position   = position;
            st.pot_size_chips   = SMALL_BLIND_CHIPS + BIG_BLIND_CHIPS;
            st.stack_size_chips = STARTING_STACK_CHIPS;
            st.street = Street::PREFLOP;

            // Preflop start-ish states:
            if (position == 0) { // SB/Button acts first; facing BB (2) with SB posted (1)
                st.to_call_chips = BIG_BLIND_CHIPS - SMALL_BLIND_CHIPS; // 1 to complete
                st.can_fold = true; st.can_check_call = true; st.can_raise = true;
            } else { // BB unopened (proxy for BB open-raise tendency)
                st.to_call_chips = 0;
                st.can_fold = false; st.can_check_call = true; st.can_raise = true;
            }
            st.min_raise_chips = BIG_BLIND_CHIPS; st.max_raise_chips = STARTING_STACK_CHIPS;

            auto p = f->policy_probs(st);
            // p[2..8] are raise buckets, p[9] is all-in; “open-raise probability”:
            double open = 0.0;
            for (int i=2;i<OUTPUTS;++i) open += p[i];

            std::string hk = hand_key(cards[0], cards[1]);
            acc[hk] += open; count[hk] += 1;
        }
        std::map<std::string,double> avg;
        for (auto& kv: acc) avg[kv.first] = kv.second / double(count[kv.first]);
        write_range_csv(prefix + (position==0?"_SB_open.csv":"_BB_open.csv"), avg);
    };

    eval_side(0);
    eval_side(1);
}

// --------------------------- Evolution manager -------------------------------
class EvolutionManager {
private:
    std::vector<std::unique_ptr<PokerBot>> population;
    XorShift64 rng;
    int survivor_count = 50;

public:
    EvolutionManager(int pop_size, XorShift64& rng_seed, int survivors = 50)
        : rng(rng_seed), survivor_count(survivors) {
        // INPUT_FEATURES → 128 → 64 → OUTPUTS(= 2 + 7 + 1 = 10)
        std::vector<int> layers = {INPUT_FEATURES, 128, 64, OUTPUTS};
        for (int i = 0; i < pop_size; ++i)
            population.push_back(std::make_unique<FeedForwardBot>(layers, rng));
    }

    void run_generation(int hands_per_matchup) {
        HeadsUpGame game(rng);
        for (auto& b : population) { b->total_bb_won = 0.0; b->hands_played = 0; }

        const int total_hands = int(population.size()) * hands_per_matchup;
        for (int h = 0; h < total_hands; ++h) {
            std::size_t i = rng.uniform((uint32_t)population.size());
            std::size_t j = rng.uniform((uint32_t)population.size());
            if (i == j) j = (j + 1) % population.size();

            double res_bb = game.play_hand(*population[i], *population[j]);
            population[i]->total_bb_won += res_bb;
            population[j]->total_bb_won -= res_bb;
            population[i]->hands_played++;
            population[j]->hands_played++;
        }
    }

    void evolve() {
        std::sort(population.begin(), population.end(),
                  [](const auto& a, const auto& b) {
                      return a->avg_bb_per_hand() > b->avg_bb_per_hand();
                  });
        std::vector<std::unique_ptr<PokerBot>> next;
        int survivors = std::min(survivor_count, (int)population.size());
        for (int i = 0; i < survivors; ++i)
            next.push_back(population[i]->clone());
        while (next.size() < population.size()) {
            int p = rng.uniform((uint32_t)survivors);
            auto child = population[p]->clone();
            child->mutate(rng);
            next.push_back(std::move(child));
        }
        population = std::move(next);
    }

    void print_stats_both() const {
        std::vector<double> fit; fit.reserve(population.size());
        for (auto& b : population) fit.push_back(b->avg_bb_per_hand());

        GenStats all  = compute_stats_from(fit);
        std::size_t k = std::min<std::size_t>(survivor_count, fit.size());
        GenStats topk = compute_stats_top_k(fit, k);

        std::cout << "Generation Stats (entire population):\n";
        std::cout << "  Best:   " << all.best   << " BB/hand\n";
        std::cout << "  Median: " << all.median << " BB/hand\n";
        std::cout << "  Worst:  " << all.worst  << " BB/hand\n";
        std::cout << "  Mean:   " << all.mean   << " BB/hand\n";
        std::cout << "Top-" << k << " (survivor candidates):\n";
        std::cout << "  Best:   " << topk.best   << " BB/hand\n";
        std::cout << "  Median: " << topk.median << " BB/hand\n";
        std::cout << "  Worst:  " << topk.worst  << " BB/hand\n";
        std::cout << "  Mean:   " << topk.mean   << " BB/hand\n\n";
    }

    std::string one_logged_random_hand() {
        if (population.size() < 2) return "Population too small.";
        std::size_t i = rng.uniform((uint32_t)population.size());
        std::size_t j = rng.uniform((uint32_t)population.size());
        if (i == j) j = (j + 1) % population.size();
        HeadsUpGame game(rng);
        return game.play_hand_logged(*population[i], *population[j]);
    }

    // Accessor for analysis
    const std::vector<std::unique_ptr<PokerBot>>& population_ref() const { return population; }
};

// --------------------------- main --------------------------------------------
int main() {
    XorShift64 seed{ 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(std::time(nullptr)) };
    XorShift64 rng = seed;

    const int POP = 100;
    const int SURVIVORS = 33;
    EvolutionManager evo(POP, rng, SURVIVORS);

    for (int gen = 0; gen < 500; ++gen) {
        evo.run_generation(/*hands_per_matchup=*/500);
        evo.print_stats_both();
        evo.evolve();
    }

    std::cout << "=== Random hand from final generation ===\n";
    std::cout << evo.one_logged_random_hand() << std::endl;

    // Export preflop open-raise ranges for the best model of final population
    const auto& pop = evo.population_ref();
    if (!pop.empty()) {
        dump_preflop_ranges_csv(*pop.front(), "ranges_final_top1");
        std::cout << "Wrote: ranges_final_top1_SB_open.csv and ranges_final_top1_BB_open.csv\n";
    }

    return 0;
}
