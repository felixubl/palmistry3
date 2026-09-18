//! 4×13 bitboard: 4 suits, 13 ranks per suit.

use crate::card::{Card, Suit};

pub const MASK13: u16 = (1u16 << 13) - 1;

#[derive(Copy, Clone, Debug, Eq, PartialEq, Default)]
pub struct BitBoard4x13 {
    suits: [u16; 4],
}

impl BitBoard4x13 {
    #[inline(always)]
    pub const fn new() -> Self {
        Self { suits: [0; 4] }
    }

    #[inline(always)]
    pub fn clear(&mut self) {
        self.suits = [0; 4];
    }

    #[inline]
    pub fn from_cards<I: IntoIterator<Item = Card>>(iter: I) -> Self {
        let mut b = Self::new();
        for c in iter {
            let _ = b.add_card(c);
        }
        b
    }

    #[inline(always)]
    pub const fn suits_array(&self) -> &[u16; 4] {
        &self.suits
    }

    #[inline(always)]
    pub const fn suit_mask(&self, suit: Suit) -> u16 {
        self.suits[suit.idx()]
    }

    /// Returns true if card was already present.
    #[inline(always)]
    pub fn add_card(&mut self, card: Card) -> bool {
        let s = card.suit.idx();
        let bit = 1u16 << (card.rank.idx() as u16);
        let old = self.suits[s];
        let already = (old & bit) != 0;
        self.suits[s] = (old | bit) & MASK13;
        already
    }

    #[inline(always)]
    pub fn add_id(&mut self, id: u8) -> bool {
        let s = (id / 13) as usize;
        let r = (id % 13) as u16;
        let bit = 1u16 << r;
        let old = self.suits[s];
        let already = (old & bit) != 0;
        self.suits[s] = (old | bit) & MASK13;
        already
    }

    #[inline(always)]
    pub fn remove_card(&mut self, card: Card) {
        let s = card.suit.idx();
        let bit = 1u16 << (card.rank.idx() as u16);
        self.suits[s] = (self.suits[s] & !bit) & MASK13;
    }

    #[inline(always)]
    pub fn ranks_any(&self) -> u16 {
        self.suits[0] | self.suits[1] | self.suits[2] | self.suits[3]
    }

    #[inline(always)]
    pub fn ge2(&self) -> u16 {
        let h0 = self.suits[0];
        let h1 = self.suits[1];
        let h2 = self.suits[2];
        let h3 = self.suits[3];
        (h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)
    }

    #[inline(always)]
    pub fn ge3(&self) -> u16 {
        let h0 = self.suits[0];
        let h1 = self.suits[1];
        let h2 = self.suits[2];
        let h3 = self.suits[3];
        (h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)
    }

    #[inline(always)]
    pub fn ge4(&self) -> u16 {
        self.suits[0] & self.suits[1] & self.suits[2] & self.suits[3]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::card::{Card, Rank::*, Suit::*};

    #[test]
    fn add_contains_remove_idempotent() {
        let mut b = BitBoard4x13::new();
        let c = Card::new(Spades, Ace);

        assert_eq!(b.add_card(c), false);
        assert_eq!(b.add_card(c), true);

        b.remove_card(c);
        assert_eq!(b.add_card(c), false);
    }

    #[test]
    fn add_id_matches_add_card() {
        let mut a = BitBoard4x13::new();
        let mut b = BitBoard4x13::new();

        let id = Card::new(Hearts, Ten).id();
        a.add_id(id);
        b.add_card(Card::from_id(id));

        assert_eq!(a, b);
    }

    #[test]
    fn multiplicity_masks() {
        let mut b = BitBoard4x13::new();
        // Four-of-a-kind by rank (across suits)
        b.add_card(Card::new(Clubs, Two));
        b.add_card(Card::new(Diamonds, Two));
        b.add_card(Card::new(Hearts, Two));
        b.add_card(Card::new(Spades, Two));

        assert_eq!(b.ge2().count_ones(), 1);
        assert_eq!(b.ge3().count_ones(), 1);
        assert_eq!(b.ge4().count_ones(), 1);
    }
}
