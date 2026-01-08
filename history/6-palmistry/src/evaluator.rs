//! LUT-based evaluator over BitBoard4x13.
//!
//! This is the Rust analogue of your Python evaluator core: compute suit masks,
//! rank union, multiplicity masks, then decide the best category, finally pack
//! into a u32 Score.

use crate::bitboard::{BitBoard4x13, MASK13};
use crate::lut13::{hibit13, popcnt13, straight_end13};
use crate::score::{pack_score, Category, Score};

#[inline(always)]
fn has_two_or_more_bits(x: u16) -> bool {
    x != 0 && (x & (x - 1)) != 0
}

#[inline(always)]
fn top5_rank_indices_from_mask(mut m: u16) -> [u8; 5] {
    // Return 5 indices (hi..lo). For flush/high-card paths we should always have >=5 ranks.
    let mut out = [0u8; 5];
    m &= MASK13;

    let mut i = 0usize;
    while i < 5 {
        // pick MSB
        let idx = hibit13(m);
        out[i] = idx as u8;

        // clear that MSB bit
        m &= !(1u16 << (idx as u16));
        i += 1;
    }
    out
}

#[inline(always)]
pub fn evaluate_u32(hand: &BitBoard4x13) -> Score {
    let h = hand.suits_array();
    let h0 = h[0] & MASK13;
    let h1 = h[1] & MASK13;
    let h2 = h[2] & MASK13;
    let h3 = h[3] & MASK13;

    let ranks: u16 = (h0 | h1 | h2 | h3) & MASK13;

    // Multiplicity masks (by rank across suits)
    let ge4: u16 = (h0 & h1 & h2 & h3) & MASK13;
    let ge2: u16 = ((h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)) & MASK13;
    let ge3: u16 = ((h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)) & MASK13;

    // Straight flush
    let mut best_sf: i8 = -1;
    if popcnt13(h0) >= 5 {
        let se = straight_end13(h0);
        if se > best_sf {
            best_sf = se;
        }
    }
    if popcnt13(h1) >= 5 {
        let se = straight_end13(h1);
        if se > best_sf {
            best_sf = se;
        }
    }
    if popcnt13(h2) >= 5 {
        let se = straight_end13(h2);
        if se > best_sf {
            best_sf = se;
        }
    }
    if popcnt13(h3) >= 5 {
        let se = straight_end13(h3);
        if se > best_sf {
            best_sf = se;
        }
    }
    if best_sf >= 0 {
        return pack_score(Category::StraightFlush, best_sf as u8, 0, 0, 0, 0);
    }

    // Quads
    if ge4 != 0 {
        let qr = hibit13(ge4) as u8;
        let kmask = ranks & !(1u16 << (qr as u16));
        let kr = hibit13(kmask) as u8;
        return pack_score(Category::Quads, qr, kr, 0, 0, 0);
    }

    // Full house (either trips+pair or two trips)
    let exactly3 = ge3 & !ge4;
    if exactly3 != 0 {
        let tr1 = hibit13(exactly3) as u8;

        // pair ranks are >=2 but not >=3
        let pairs_only = ge2 & !ge3;
        let pmask = pairs_only & !(1u16 << (tr1 as u16));
        let pr = hibit13(pmask);
        if pr >= 0 {
            return pack_score(Category::FullHouse, tr1, pr as u8, 0, 0, 0);
        }

        // second trips
        let tr2mask = exactly3 & !(1u16 << (tr1 as u16));
        let tr2 = hibit13(tr2mask);
        if tr2 >= 0 {
            return pack_score(Category::FullHouse, tr1, tr2 as u8, 0, 0, 0);
        }
    }

    // Flush
    let mut flush_mask: u16 = 0;
    if popcnt13(h0) >= 5 {
        flush_mask = h0;
    } else if popcnt13(h1) >= 5 {
        flush_mask = h1;
    } else if popcnt13(h2) >= 5 {
        flush_mask = h2;
    } else if popcnt13(h3) >= 5 {
        flush_mask = h3;
    }

    if flush_mask != 0 {
        let t = top5_rank_indices_from_mask(flush_mask);
        return pack_score(Category::Flush, t[0], t[1], t[2], t[3], t[4]);
    }

    // Straight
    let se = straight_end13(ranks);
    if se >= 0 {
        return pack_score(Category::Straight, se as u8, 0, 0, 0, 0);
    }

    // Trips
    let trips = ge3 & !ge4;
    if trips != 0 {
        let tr = hibit13(trips) as u8;
        let mut kmask = ranks & !(1u16 << (tr as u16));
        let k1 = hibit13(kmask) as u8;
        kmask &= kmask - 1;
        let k2 = hibit13(kmask) as u8;
        return pack_score(Category::Trips, tr, k1, k2, 0, 0);
    }

    // Pairs (two pair / one pair)
    let pairs = ge2 & !ge3;
    if has_two_or_more_bits(pairs) {
        let p1 = hibit13(pairs) as u8;
        let pmask = pairs & !(1u16 << (p1 as u16));
        let p2 = hibit13(pmask) as u8;
        let kmask = ranks & !((1u16 << (p1 as u16)) | (1u16 << (p2 as u16)));
        let k = hibit13(kmask) as u8;
        return pack_score(Category::TwoPair, p1, p2, k, 0, 0);
    }

    if pairs != 0 {
        let pr = hibit13(pairs) as u8;
        let mut kmask = ranks & !(1u16 << (pr as u16));
        let k1 = hibit13(kmask) as u8;
        kmask &= kmask - 1;
        let k2 = hibit13(kmask) as u8;
        kmask &= kmask - 1;
        let k3 = hibit13(kmask) as u8;
        return pack_score(Category::OnePair, pr, k1, k2, k3, 0);
    }

    // High card (no pairs => at least 5 distinct ranks)
    let t = top5_rank_indices_from_mask(ranks);
    pack_score(Category::HighCard, t[0], t[1], t[2], t[3], t[4])
}

/// Convenience: evaluate from a small slice of 0..51 card ids.
#[inline]
pub fn evaluate_u32_from_ids(ids: &[u8]) -> Score {
    let mut b = BitBoard4x13::new();
    for &id in ids {
        b.add_id(id);
    }
    evaluate_u32(&b)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::card::{Card, Rank::*, Suit::*};
    use crate::score::Category;

    #[test]
    fn categories_basic() {
        // Royal flush (straight flush end=12)
        let rf = BitBoard4x13::from_cards([
            Card::new(Spades, Ten),
            Card::new(Spades, Jack),
            Card::new(Spades, Queen),
            Card::new(Spades, King),
            Card::new(Spades, Ace),
            Card::new(Hearts, Two),
            Card::new(Diamonds, Three),
        ]);
        let s = evaluate_u32(&rf);
        assert_eq!((s.0 >> 20) as u8, Category::StraightFlush as u8);

        // Quads: four twos + kicker Ace
        let quads = BitBoard4x13::from_cards([
            Card::new(Clubs, Two),
            Card::new(Diamonds, Two),
            Card::new(Hearts, Two),
            Card::new(Spades, Two),
            Card::new(Clubs, Ace),
            Card::new(Hearts, King),
            Card::new(Diamonds, Queen),
        ]);
        let s = evaluate_u32(&quads);
        assert_eq!((s.0 >> 20) as u8, Category::Quads as u8);

        // Wheel straight (A2345): end = 3
        let wheel = BitBoard4x13::from_cards([
            Card::new(Clubs, Ace),
            Card::new(Diamonds, Two),
            Card::new(Hearts, Three),
            Card::new(Spades, Four),
            Card::new(Clubs, Five),
        ]);
        let s = evaluate_u32(&wheel);
        assert_eq!((s.0 >> 20) as u8, Category::Straight as u8);
        let end = ((s.0 >> 16) & 0xF) as u8;
        assert_eq!(end, 3);
    }
}
