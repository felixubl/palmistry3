//! Card types (Suit/Rank/Card).
//!
//! These are "zero-cost" abstractions: small enums and a small struct that
//! compile down to integer operations.

/// A playing card suit.
#[repr(u8)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub enum Suit {
    Clubs = 0,
    Diamonds = 1,
    Hearts = 2,
    Spades = 3,
}

impl Suit {
    pub const ALL: [Suit; 4] = [Suit::Clubs, Suit::Diamonds, Suit::Hearts, Suit::Spades];

    #[inline(always)]
    pub const fn idx(self) -> usize {
        self as usize
    }

    #[inline(always)]
    pub const fn from_u8(x: u8) -> Suit {
        match x & 0x3 {
            0 => Suit::Clubs,
            1 => Suit::Diamonds,
            2 => Suit::Hearts,
            _ => Suit::Spades,
        }
    }
}

/// A playing card rank.
///
/// We store ranks as 0..12 (Two..Ace). This matches a 13-bit mask naturally.
#[repr(u8)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum Rank {
    Two = 0,
    Three = 1,
    Four = 2,
    Five = 3,
    Six = 4,
    Seven = 5,
    Eight = 6,
    Nine = 7,
    Ten = 8,
    Jack = 9,
    Queen = 10,
    King = 11,
    Ace = 12,
}

impl Rank {
    pub const ALL: [Rank; 13] = [
        Rank::Two,
        Rank::Three,
        Rank::Four,
        Rank::Five,
        Rank::Six,
        Rank::Seven,
        Rank::Eight,
        Rank::Nine,
        Rank::Ten,
        Rank::Jack,
        Rank::Queen,
        Rank::King,
        Rank::Ace,
    ];

    #[inline(always)]
    pub const fn idx(self) -> u8 {
        self as u8
    }

    /// Convert 0..12 to a Rank (Two..Ace). Input is assumed valid.
    #[inline(always)]
    pub const fn from_u8(x: u8) -> Rank {
        match x {
            0 => Rank::Two,
            1 => Rank::Three,
            2 => Rank::Four,
            3 => Rank::Five,
            4 => Rank::Six,
            5 => Rank::Seven,
            6 => Rank::Eight,
            7 => Rank::Nine,
            8 => Rank::Ten,
            9 => Rank::Jack,
            10 => Rank::Queen,
            11 => Rank::King,
            _ => Rank::Ace,
        }
    }
}

/// A card = suit + rank.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct Card {
    pub suit: Suit,
    pub rank: Rank,
}

impl Card {
    #[inline(always)]
    pub const fn new(suit: Suit, rank: Rank) -> Self {
        Self { suit, rank }
    }

    /// Pack into a single byte-ish integer: (suit << 4) | rank.
    #[inline(always)]
    pub const fn pack(self) -> u8 {
        ((self.suit as u8) << 4) | (self.rank as u8)
    }

    /// Unpack from (suit << 4) | rank. Input assumed valid.
    #[inline(always)]
    pub const fn unpack(x: u8) -> Self {
        let suit = Suit::from_u8((x >> 4) & 0x3);
        let rank = Rank::from_u8(x & 0xF);
        Self { suit, rank }
    }

    /// Standard 0..51 card id mapping:
    /// suit = id / 13, rank = id % 13.
    #[inline(always)]
    pub const fn from_id(id: u8) -> Self {
        let suit = Suit::from_u8(id / 13);
        let rank = Rank::from_u8(id % 13);
        Self { suit, rank }
    }

    /// Convert to 0..51 id (inverse of from_id).
    #[inline(always)]
    pub const fn id(self) -> u8 {
        (self.suit as u8) * 13 + (self.rank as u8)
    }
}
