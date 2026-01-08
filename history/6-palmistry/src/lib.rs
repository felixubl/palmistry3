//! Poker evaluation/simulation core library.

pub mod card;
pub mod bitboard;
pub mod lut13;
pub mod score;
pub mod evaluator;
pub mod batch;
pub mod equity;

pub use card::{parse_board, parse_hand, parse_hole_cards, Card, Rank, Suit};
pub use bitboard::{BitBoard4x13, MASK13};
pub use evaluator::{evaluate_u32, evaluate_u32_from_ids};
pub use score::{Category, Score};

pub use equity::{
    compare_showdown_checked,
    compare_showdown_unchecked,
    equity_exact_multiway_checked,
    equity_exact_vs_hand_checked,
    equity_exact_vs_random_checked,
    equity_mc_multiway_checked,
    equity_mc_vs_hand_checked,
    equity_mc_vs_random_checked,
    equity_mc_vs_random_multiway_checked,
    EquityCounts,
    EquityError,
    MultiWayResult,
    Outcome,
};
