use once_cell::sync::Lazy;

pub type Hand = [u16; 4];

pub const MASK13: u16 = (1 << 13) - 1;
pub const WHEEL_MASK: u16 = (1 << 12) | (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);

const RUN5: [u16; 9] = {
    let mut a = [0u16; 9];
    let mut i = 0;
    while i < 9 {
        a[i] = (((1u16 << 5) - 1) << i) & MASK13;
        i += 1;
    }
    a
};

// --- LUTs (8192 entries for 13-bit masks) -----------------------------------

pub static POPCNT13: Lazy<[u8; 8192]> = Lazy::new(|| {
    let mut t = [0u8; 8192];
    let mut m = 0usize;
    while m < 8192 {
        t[m] = (m as u16).count_ones() as u8;
        m += 1;
    }
    t
});

pub static HIBIT13: Lazy<[i8; 8192]> = Lazy::new(|| {
    let mut t = [0i8; 8192];
    t[0] = -1;
    let mut m = 1usize;
    while m < 8192 {
        t[m] = (15 - (m as u16).leading_zeros() as i32) as i8; // floor(log2(m))
        m += 1;
    }
    t
});

pub static STRAIGHT_END13: Lazy<[i16; 8192]> = Lazy::new(|| {
    let mut t = [-1i16; 8192];
    let mut m = 0usize;
    while m < 8192 {
        let mut end = -1i16;
        let mm = m as u16;
        let mut s = 8i16;
        while s >= 0 {
            let w = RUN5[s as usize];
            if (mm & w) == w {
                end = s + 4;
                break;
            }
            s -= 1;
        }
        if end < 3 && ((mm & WHEEL_MASK) == WHEEL_MASK) {
            end = 3;
        }
        t[m] = end;
        m += 1;
    }
    t
});

// --- small helpers -----------------------------------------------------------

#[inline]
pub fn card_to_int(suit: i32, rank: i32) -> i32 {
    (suit << 4) | rank
}

#[inline]
pub fn decode_card(card: i32) -> (i32, i32) {
    let rank = card & 0xF;
    let suit = (card >> 4) & 0x3;
    (suit, rank)
}

#[inline]
pub fn empty_hand() -> Hand {
    [0u16; 4]
}

/// Set the card bit. Returns 0 if newly added, 1 if already present.
pub fn add_card(hand: &mut Hand, card: i32) -> i32 {
    let (suit, rank) = decode_card(card);
    let bit = 1u16 << (rank as u16);
    let old = hand[suit as usize];
    if (old & bit) != 0 {
        return 1;
    }
    hand[suit as usize] = (old | bit) & MASK13;
    0
}

#[inline]
pub fn popcnt13(mask: u16) -> i32 {
    POPCNT13[mask as usize] as i32
}

#[inline]
pub fn straight_end_from_mask(mask: u16) -> i32 {
    STRAIGHT_END13[mask as usize] as i32
}

#[inline]
pub fn msb_index(mask: u16) -> i32 {
    HIBIT13[mask as usize] as i32
}

/// Return the top five ranks (hi..lo) present in mask; fill with -1 if fewer.
/// NOTE: after reading MSB r, we must clear **that** bit: m &= !(1<<r).
#[inline]
pub fn top5_from_mask(mut m: u16) -> (i32, i32, i32, i32, i32) {
    let mut out = [-1i32; 5];
    let mut i = 0usize;
    while i < 5 {
        let r = msb_index(m);
        out[i] = r;
        if r < 0 {
            break;
        }
        m &= !(1u16 << (r as u16));
        i += 1;
    }
    (out[0], out[1], out[2], out[3], out[4])
}

/// Pack cat + 5 tiebreakers into u32 (higher is better).
#[inline]
pub fn pack_score(cat: i32, r0: i32, r1: i32, r2: i32, r3: i32, r4: i32) -> u32 {
    ((cat as u32) << 20)
        | ((r0 as u32) << 16)
        | ((r1 as u32) << 12)
        | ((r2 as u32) << 8)
        | ((r3 as u32) << 4)
        | ((r4 as u32) & 0xF)
}

/// Optional compatibility helper.
#[inline]
pub fn unpack_score(s: u32) -> (i32, i32, i32, i32, i32, i32) {
    let s = s as i32;
    let cat = (s >> 20) & 0xF;
    let r0 = (s >> 16) & 0xF;
    let r1 = (s >> 12) & 0xF;
    let r2 = (s >> 8) & 0xF;
    let r3 = (s >> 4) & 0xF;
    let r4 = s & 0xF;
    (cat, r0, r1, r2, r3, r4)
}

/// Evaluate 7 cards in bitboard form and return a packed u32 strength.
#[inline]
pub fn evaluate_u32(hand: &Hand) -> u32 {
    let (h0, h1, h2, h3) = (hand[0], hand[1], hand[2], hand[3]);

    let ranks: u16 = (h0 | h1 | h2 | h3) & MASK13;
    let ge4: u16 = (h0 & h1 & h2 & h3) & MASK13;
    let ge2: u16 = ((h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)) & MASK13;
    let ge3: u16 = ((h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)) & MASK13;

    // Straight flush
    let mut best_sf = -1;
    if popcnt13(h0) >= 5 {
        let se = straight_end_from_mask(h0);
        if se > best_sf { best_sf = se; }
    }
    if popcnt13(h1) >= 5 {
        let se = straight_end_from_mask(h1);
        if se > best_sf { best_sf = se; }
    }
    if popcnt13(h2) >= 5 {
        let se = straight_end_from_mask(h2);
        if se > best_sf { best_sf = se; }
    }
    if popcnt13(h3) >= 5 {
        let se = straight_end_from_mask(h3);
        if se > best_sf { best_sf = se; }
    }
    if best_sf >= 0 {
        return pack_score(8, best_sf, 0, 0, 0, 0);
    }

    // Four of a kind
    let all4 = ge4;
    if all4 != 0 {
        let qr = msb_index(all4);
        let kmask = (ranks & !(1u16 << (qr as u16))) & MASK13;
        let kr = msb_index(kmask);
        return pack_score(7, qr, kr, 0, 0, 0);
    }

    // Full house
    let exactly3 = ge3 & !ge4 & MASK13;
    if exactly3 != 0 {
        let tr1 = msb_index(exactly3);
        let pairs_only = ge2 & !ge3 & MASK13;
        let pmask = (pairs_only & !(1u16 << (tr1 as u16))) & MASK13;
        let pr = msb_index(pmask);
        if pr >= 0 {
            return pack_score(6, tr1, pr, 0, 0, 0);
        }
        let tr2mask = (exactly3 & !(1u16 << (tr1 as u16))) & MASK13;
        let tr2 = msb_index(tr2mask);
        if tr2 >= 0 {
            return pack_score(6, tr1, tr2, 0, 0, 0);
        }
    }

    // Flush (top-5 in-suit)
    let mut m: u16 = 0;
    if popcnt13(h0) >= 5 { m = h0; }
    else if popcnt13(h1) >= 5 { m = h1; }
    else if popcnt13(h2) >= 5 { m = h2; }
    else if popcnt13(h3) >= 5 { m = h3; }
    if m != 0 {
        let (r0, r1, r2, r3, r4) = top5_from_mask(m);
        return pack_score(5, r0, r1, r2, r3, r4);
    }

    // Straight
    let se = straight_end_from_mask(ranks);
    if se >= 0 {
        return pack_score(4, se, 0, 0, 0, 0);
    }

    // Three of a kind
    let trips = ge3 & !ge4 & MASK13;
    if trips != 0 {
        let tr = msb_index(trips);
        let mut kmask = (ranks & !(1u16 << (tr as u16))) & MASK13;
        let k1 = msb_index(kmask);
        if k1 >= 0 { kmask &= !(1u16 << (k1 as u16)); }
        let k2 = msb_index(kmask);
        return pack_score(3, tr, k1, k2, 0, 0);
    }

    // Two pair
    let pairs = ge2 & !ge3 & MASK13;
    if pairs != 0 && (pairs & (pairs - 1)) != 0 {
        let p1 = msb_index(pairs);
        let pmask = (pairs & !(1u16 << (p1 as u16))) & MASK13;
        let p2 = msb_index(pmask);
        let kmask = (ranks & !( (1u16 << (p1 as u16)) | (1u16 << (p2 as u16)) )) & MASK13;
        let k = msb_index(kmask);
        return pack_score(2, p1, p2, k, 0, 0);
    }

    // One pair
    if pairs != 0 {
        let pr = msb_index(pairs);
        let mut kmask = (ranks & !(1u16 << (pr as u16))) & MASK13;
        let k1 = msb_index(kmask);
        if k1 >= 0 { kmask &= !(1u16 << (k1 as u16)); }
        let k2 = msb_index(kmask);
        if k2 >= 0 { kmask &= !(1u16 << (k2 as u16)); }
        let k3 = msb_index(kmask);
        return pack_score(1, pr, k1, k2, k3, 0);
    }

    // High card
    let r0 = msb_index(ranks);
    let mut m2 = ranks & !(1u16 << (r0.max(0) as u16));
    let r1 = msb_index(m2);
    if r1 >= 0 { m2 &= !(1u16 << (r1 as u16)); }
    let r2 = msb_index(m2);
    if r2 >= 0 { m2 &= !(1u16 << (r2 as u16)); }
    let r3 = msb_index(m2);
    if r3 >= 0 { m2 &= !(1u16 << (r3 as u16)); }
    let r4 = msb_index(m2);
    pack_score(0, r0, r1, r2, r3, r4)
}
