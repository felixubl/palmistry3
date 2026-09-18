//! Card representation and parsing.

use std::fmt;
use std::str::FromStr;

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

impl fmt::Display for Suit {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let c = match self {
            Suit::Clubs => 'c',
            Suit::Diamonds => 'd',
            Suit::Hearts => 'h',
            Suit::Spades => 's',
        };
        write!(f, "{}", c)
    }
}

impl FromStr for Suit {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s.chars().count() != 1 {
            return Err(format!("Invalid suit: '{}' (must be single character)", s));
        }
        match s.chars().next().unwrap() {
            'c' | 'C' | '♣' => Ok(Suit::Clubs),
            'd' | 'D' | '♦' => Ok(Suit::Diamonds),
            'h' | 'H' | '♥' => Ok(Suit::Hearts),
            's' | 'S' | '♠' => Ok(Suit::Spades),
            c => Err(format!("Invalid suit: '{}' (expected c/d/h/s)", c)),
        }
    }
}

/// Ranks encoded as 0..12 (Two..Ace).
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

impl fmt::Display for Rank {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let c = match self {
            Rank::Two => '2',
            Rank::Three => '3',
            Rank::Four => '4',
            Rank::Five => '5',
            Rank::Six => '6',
            Rank::Seven => '7',
            Rank::Eight => '8',
            Rank::Nine => '9',
            Rank::Ten => 'T',
            Rank::Jack => 'J',
            Rank::Queen => 'Q',
            Rank::King => 'K',
            Rank::Ace => 'A',
        };
        write!(f, "{}", c)
    }
}

impl FromStr for Rank {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s.chars().count() != 1 {
            return Err(format!("Invalid rank: '{}' (must be single character)", s));
        }
        match s.chars().next().unwrap() {
            '2' => Ok(Rank::Two),
            '3' => Ok(Rank::Three),
            '4' => Ok(Rank::Four),
            '5' => Ok(Rank::Five),
            '6' => Ok(Rank::Six),
            '7' => Ok(Rank::Seven),
            '8' => Ok(Rank::Eight),
            '9' => Ok(Rank::Nine),
            'T' | 't' => Ok(Rank::Ten),
            'J' | 'j' => Ok(Rank::Jack),
            'Q' | 'q' => Ok(Rank::Queen),
            'K' | 'k' => Ok(Rank::King),
            'A' | 'a' => Ok(Rank::Ace),
            c => Err(format!("Invalid rank: '{}' (expected 2-9/T/J/Q/K/A)", c)),
        }
    }
}

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

    #[inline(always)]
    pub const fn pack(self) -> u8 {
        ((self.suit as u8) << 4) | (self.rank as u8)
    }

    #[inline(always)]
    pub const fn unpack(x: u8) -> Self {
        let suit = Suit::from_u8((x >> 4) & 0x3);
        let rank = Rank::from_u8(x & 0xF);
        Self { suit, rank }
    }

    /// Card ID in range 0..51 where id = suit * 13 + rank.
    #[inline(always)]
    pub const fn from_id(id: u8) -> Self {
        let suit = Suit::from_u8(id / 13);
        let rank = Rank::from_u8(id % 13);
        Self { suit, rank }
    }

    #[inline(always)]
    pub const fn id(self) -> u8 {
        (self.suit as u8) * 13 + (self.rank as u8)
    }
}

impl fmt::Display for Card {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}{}", self.rank, self.suit)
    }
}

impl FromStr for Card {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let chars: Vec<char> = s.chars().collect();
        if chars.len() != 2 {
            return Err(format!(
                "Invalid card: '{}' (must be 2 characters, e.g., 'As', 'Kh')",
                s
            ));
        }
        let rank = Rank::from_str(&chars[0].to_string())?;
        let suit = Suit::from_str(&chars[1].to_string())?;
        Ok(Card::new(suit, rank))
    }
}

/// Parse space-separated cards.
pub fn parse_hand(s: &str) -> Result<Vec<Card>, String> {
    s.split_whitespace()
        .map(|card_str| Card::from_str(card_str))
        .collect()
}

/// Parse exactly 2 cards. Accepts "As Kh" or "AsKh".
pub fn parse_hole_cards(s: &str) -> Result<[Card; 2], String> {
    let trimmed = s.trim();

    if trimmed.contains(char::is_whitespace) {
        let cards = parse_hand(trimmed)?;
        if cards.len() != 2 {
            return Err(format!(
                "Expected exactly 2 cards, got {} (input: '{}')",
                cards.len(),
                s
            ));
        }
        return Ok([cards[0], cards[1]]);
    }

    if trimmed.len() == 4 {
        let c1 = Card::from_str(&trimmed[0..2])?;
        let c2 = Card::from_str(&trimmed[2..4])?;
        return Ok([c1, c2]);
    }

    Err(format!(
        "Invalid hole cards format: '{}' (expected 'As Kh' or 'AsKh')",
        s
    ))
}

/// Parse 0-5 board cards.
pub fn parse_board(s: &str) -> Result<Vec<Card>, String> {
    if s.trim().is_empty() {
        return Ok(vec![]);
    }
    let cards = parse_hand(s)?;
    if cards.len() > 5 {
        return Err(format!("Too many board cards: {} (max 5)", cards.len()));
    }
    Ok(cards)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn suit_display() {
        assert_eq!(Suit::Clubs.to_string(), "c");
        assert_eq!(Suit::Diamonds.to_string(), "d");
        assert_eq!(Suit::Hearts.to_string(), "h");
        assert_eq!(Suit::Spades.to_string(), "s");
    }

    #[test]
    fn suit_from_str() {
        assert_eq!(Suit::from_str("c").unwrap(), Suit::Clubs);
        assert_eq!(Suit::from_str("C").unwrap(), Suit::Clubs);
        assert_eq!(Suit::from_str("d").unwrap(), Suit::Diamonds);
        assert_eq!(Suit::from_str("h").unwrap(), Suit::Hearts);
        assert_eq!(Suit::from_str("s").unwrap(), Suit::Spades);
        assert_eq!(Suit::from_str("♠").unwrap(), Suit::Spades);

        assert!(Suit::from_str("x").is_err());
        assert!(Suit::from_str("cd").is_err());
    }

    #[test]
    fn rank_display() {
        assert_eq!(Rank::Two.to_string(), "2");
        assert_eq!(Rank::Ten.to_string(), "T");
        assert_eq!(Rank::Jack.to_string(), "J");
        assert_eq!(Rank::Queen.to_string(), "Q");
        assert_eq!(Rank::King.to_string(), "K");
        assert_eq!(Rank::Ace.to_string(), "A");
    }

    #[test]
    fn rank_from_str() {
        assert_eq!(Rank::from_str("2").unwrap(), Rank::Two);
        assert_eq!(Rank::from_str("9").unwrap(), Rank::Nine);
        assert_eq!(Rank::from_str("T").unwrap(), Rank::Ten);
        assert_eq!(Rank::from_str("t").unwrap(), Rank::Ten);
        assert_eq!(Rank::from_str("J").unwrap(), Rank::Jack);
        assert_eq!(Rank::from_str("A").unwrap(), Rank::Ace);
        assert_eq!(Rank::from_str("a").unwrap(), Rank::Ace);

        assert!(Rank::from_str("1").is_err());
        assert!(Rank::from_str("10").is_err());
        assert!(Rank::from_str("x").is_err());
    }

    #[test]
    fn card_display() {
        let card = Card::new(Suit::Spades, Rank::Ace);
        assert_eq!(card.to_string(), "As");

        let card = Card::new(Suit::Hearts, Rank::Ten);
        assert_eq!(card.to_string(), "Th");

        let card = Card::new(Suit::Clubs, Rank::Two);
        assert_eq!(card.to_string(), "2c");
    }

    #[test]
    fn card_from_str() {
        let card = Card::from_str("As").unwrap();
        assert_eq!(card.suit, Suit::Spades);
        assert_eq!(card.rank, Rank::Ace);

        let card = Card::from_str("Th").unwrap();
        assert_eq!(card.suit, Suit::Hearts);
        assert_eq!(card.rank, Rank::Ten);

        let card = Card::from_str("2c").unwrap();
        assert_eq!(card.suit, Suit::Clubs);
        assert_eq!(card.rank, Rank::Two);

        // Case insensitive
        let card = Card::from_str("ks").unwrap();
        assert_eq!(card.suit, Suit::Spades);
        assert_eq!(card.rank, Rank::King);

        // Errors
        assert!(Card::from_str("A").is_err());
        assert!(Card::from_str("Asx").is_err());
        assert!(Card::from_str("1s").is_err());
    }

    #[test]
    fn card_roundtrip() {
        let card = Card::new(Suit::Diamonds, Rank::Queen);
        let s = card.to_string();
        let parsed = Card::from_str(&s).unwrap();
        assert_eq!(card, parsed);
    }

    #[test]
    fn parse_hand_basic() {
        let cards = parse_hand("As Kh").unwrap();
        assert_eq!(cards.len(), 2);
        assert_eq!(cards[0], Card::new(Suit::Spades, Rank::Ace));
        assert_eq!(cards[1], Card::new(Suit::Hearts, Rank::King));
    }

    #[test]
    fn parse_hand_five_cards() {
        let cards = parse_hand("As Kh Qd Jc Ts").unwrap();
        assert_eq!(cards.len(), 5);
    }

    #[test]
    fn parse_hole_cards_space_separated() {
        let cards = parse_hole_cards("As Kh").unwrap();
        assert_eq!(cards[0], Card::new(Suit::Spades, Rank::Ace));
        assert_eq!(cards[1], Card::new(Suit::Hearts, Rank::King));
    }

    #[test]
    fn parse_hole_cards_concatenated() {
        let cards = parse_hole_cards("AsKh").unwrap();
        assert_eq!(cards[0], Card::new(Suit::Spades, Rank::Ace));
        assert_eq!(cards[1], Card::new(Suit::Hearts, Rank::King));
    }

    #[test]
    fn parse_hole_cards_errors() {
        assert!(parse_hole_cards("As").is_err()); // too few
        assert!(parse_hole_cards("As Kh Qd").is_err()); // too many
        assert!(parse_hole_cards("AsKhQd").is_err()); // wrong length
    }

    #[test]
    fn parse_board_empty() {
        let cards = parse_board("").unwrap();
        assert_eq!(cards.len(), 0);

        let cards = parse_board("  ").unwrap();
        assert_eq!(cards.len(), 0);
    }

    #[test]
    fn parse_board_flop() {
        let cards = parse_board("As Kh Qd").unwrap();
        assert_eq!(cards.len(), 3);
    }

    #[test]
    fn parse_board_river() {
        let cards = parse_board("As Kh Qd Jc Ts").unwrap();
        assert_eq!(cards.len(), 5);
    }

    #[test]
    fn parse_board_too_many() {
        assert!(parse_board("As Kh Qd Jc Ts 9h").is_err());
    }
}
