//! Packed u32 score.
//!
//! Layout (same spirit as your Python):
//! bits 20..23 : category (0..15), higher is better
//! bits 16..19 : r0
//! bits 12..15 : r1
//! bits  8..11 : r2
//! bits  4..7  : r3
//! bits  0..3  : r4
//!
//! Each r* is a 4-bit rank index (0..12). Unused slots should be 0.

#[repr(transparent)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub struct Score(pub u32);

/// Hand categories (higher is better).
#[repr(u8)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Category {
    HighCard = 0,
    OnePair = 1,
    TwoPair = 2,
    Trips = 3,
    Straight = 4,
    Flush = 5,
    FullHouse = 6,
    Quads = 7,
    StraightFlush = 8,
}

#[inline(always)]
pub fn pack_score(cat: Category, r0: u8, r1: u8, r2: u8, r3: u8, r4: u8) -> Score {
    Score(
        ((cat as u32) << 20)
            | ((r0 as u32) << 16)
            | ((r1 as u32) << 12)
            | ((r2 as u32) << 8)
            | ((r3 as u32) << 4)
            | ((r4 as u32) & 0xF),
    )
}

#[inline(always)]
pub fn unpack_score(s: Score) -> (Category, u8, u8, u8, u8, u8) {
    let v = s.0;
    let cat = match ((v >> 20) & 0xF) as u8 {
        0 => Category::HighCard,
        1 => Category::OnePair,
        2 => Category::TwoPair,
        3 => Category::Trips,
        4 => Category::Straight,
        5 => Category::Flush,
        6 => Category::FullHouse,
        7 => Category::Quads,
        _ => Category::StraightFlush,
    };
    (
        cat,
        ((v >> 16) & 0xF) as u8,
        ((v >> 12) & 0xF) as u8,
        ((v >> 8) & 0xF) as u8,
        ((v >> 4) & 0xF) as u8,
        (v & 0xF) as u8,
    )
}
