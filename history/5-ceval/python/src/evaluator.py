import numpy as np
from numba import njit, uint16, uint32, int32

MASK13 = uint16((1 << 13) - 1)
WHEEL_MASK = (1 << 12) | (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)

POPCNT13 = np.zeros(8192, dtype=np.uint8)
STRAIGHT_END13 = np.full(8192, -1, dtype=np.int16)
HIBIT13 = np.full(8192, -1, dtype=np.int8)

_RUN5 = [((1 << 5) - 1) << s for s in range(9)]

for m in range(8192):
    POPCNT13[m] = m.bit_count()
    HIBIT13[m] = (m.bit_length() - 1) if m else -1

    end = -1
    for s in range(8, -1, -1):
        w = _RUN5[s]
        if (m & w) == w:
            end = s + 4
            break
    if end < 3 and (m & WHEEL_MASK) == WHEEL_MASK:
        end = 3
    STRAIGHT_END13[m] = end

POPCNT13 = POPCNT13.view(np.uint8)
STRAIGHT_END13 = STRAIGHT_END13.view(np.int16)
HIBIT13 = HIBIT13.view(np.int8)


@njit(cache=True)
def card_to_int(suit: int32, rank: int32) -> int32:
    """suit: 0..3, rank: 0..12 (2..A)"""
    return (suit << 4) | rank


@njit(cache=True)
def decode_card(card: int32):
    """Return (suit, rank)"""
    rank = card & 0xF
    suit = (card >> 4) & 0x3
    return suit, rank


@njit(cache=True)
def empty_hand():
    return np.zeros(4, dtype=np.uint16)


@njit(cache=True)
def add_card(hand, card: int32) -> int32:
    """Set the card bit. Returns 0 if newly added, 1 if it was already present."""
    suit, rank = decode_card(card)
    bit = uint16(1 << rank)
    old = hand[suit]
    if (old & bit) != 0:
        return 1
    hand[suit] = uint16((old | bit) & MASK13)
    return 0


@njit(cache=True)
def popcnt13(mask: uint16) -> int32:
    return int32(POPCNT13[int32(mask)])


@njit(cache=True)
def straight_end_from_mask(mask: uint16) -> int32:
    return int32(STRAIGHT_END13[int32(mask)])


@njit(cache=True)
def msb_index(mask: uint16) -> int32:
    """Highest set bit index (0..12) or -1."""
    return int32(HIBIT13[int32(mask)])


@njit(cache=True)
def top5_from_mask(mask: uint16):
    """Return the top five ranks (hi..lo) present in mask (13-bit), filling with -1 if fewer."""
    r0 = r1 = r2 = r3 = r4 = int32(-1)
    m = uint16(mask)

    r = msb_index(m)
    r0 = r
    if r >= 0:
        m = uint16(m & ~uint16(1 << r))
        r = msb_index(m); r1 = r
        if r >= 0:
            m = uint16(m & ~uint16(1 << r))
            r = msb_index(m); r2 = r
            if r >= 0:
                m = uint16(m & ~uint16(1 << r))
                r = msb_index(m); r3 = r
                if r >= 0:
                    m = uint16(m & ~uint16(1 << r))
                    r = msb_index(m); r4 = r

    return r0, r1, r2, r3, r4


@njit(cache=True)
def pack_score(
    cat: int32, r0: int32, r1: int32, r2: int32, r3: int32, r4: int32
) -> uint32:
    """Pack category and up to 5 tiebreaker ranks into a single uint32 (higher is better)."""
    def nib(v): return (v & 0xF)
    return uint32(
        (nib(cat) << 20)
      | (nib(r0)  << 16)
      | (nib(r1)  << 12)
      | (nib(r2)  <<  8)
      | (nib(r3)  <<  4)
      |  nib(r4)
    )


@njit(cache=True)
def unpack_score(score: uint32):
    """Return (cat, r0..r4) for compatibility with older API."""
    s = int32(score)
    cat = (s >> 20) & 0xF
    r0 = (s >> 16) & 0xF
    r1 = (s >> 12) & 0xF
    r2 = (s >> 8) & 0xF
    r3 = (s >> 4) & 0xF
    r4 = s & 0xF
    return cat, r0, r1, r2, r3, r4


@njit(cache=True)
def evaluate_u32(hand):
    """Evaluate 7 cards in bitboard form and return a packed uint32 strength."""
    h0, h1, h2, h3 = hand[0], hand[1], hand[2], hand[3]

    ranks = uint16((h0 | h1 | h2 | h3) & MASK13)
    ge4 = uint16((h0 & h1 & h2 & h3) & MASK13)
    ge2 = uint16(
        ((h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)) & MASK13
    )
    ge3 = uint16(
        ((h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)) & MASK13
    )

    # Straight flush
    best_sf = -1
    if popcnt13(h0) >= 5:
        se = straight_end_from_mask(h0)
        if se > best_sf:
            best_sf = se
    if popcnt13(h1) >= 5:
        se = straight_end_from_mask(h1)
        if se > best_sf:
            best_sf = se
    if popcnt13(h2) >= 5:
        se = straight_end_from_mask(h2)
        if se > best_sf:
            best_sf = se
    if popcnt13(h3) >= 5:
        se = straight_end_from_mask(h3)
        if se > best_sf:
            best_sf = se
    if best_sf >= 0:
        return pack_score(8, best_sf, 0, 0, 0, 0)

    # Quads
    all4 = ge4
    if all4 != 0:
        qr = msb_index(all4)
        kmask = uint16(ranks & ~uint16(1 << qr))
        kr = msb_index(kmask)
        return pack_score(7, qr, kr, 0, 0, 0)

    # Full house (set + pair OR two sets)
    exactly3 = uint16(ge3 & ~ge4)
    if exactly3 != 0:
        tr1 = msb_index(exactly3)
        pairs_only = uint16(ge2 & ~ge3)
        pmask = uint16(pairs_only & ~uint16(1 << tr1))
        pr = msb_index(pmask)
        if pr >= 0:
            return pack_score(6, tr1, pr, 0, 0, 0)

        tr2mask = uint16(exactly3 & ~uint16(1 << tr1))
        tr2 = msb_index(tr2mask)
        if tr2 >= 0:
            return pack_score(6, tr1, tr2, 0, 0, 0)

    # Flush
    m = uint16(0)
    if popcnt13(h0) >= 5:
        m = h0
    elif popcnt13(h1) >= 5:
        m = h1
    elif popcnt13(h2) >= 5:
        m = h2
    elif popcnt13(h3) >= 5:
        m = h3
    if m != 0:
        r0, r1, r2, r3, r4 = top5_from_mask(m)
        return pack_score(5, r0, r1, r2, r3, r4)

    # Straight
    se = straight_end_from_mask(ranks)
    if se >= 0:
        return pack_score(4, se, 0, 0, 0, 0)

    # Trips
    trips = uint16(ge3 & ~ge4)
    if trips != 0:
        tr = msb_index(trips)
        kmask = uint16(ranks & ~uint16(1 << tr))
        k1 = msb_index(kmask)
        if k1 >= 0:
            kmask = uint16(kmask & ~uint16(1 << k1))
        k2 = msb_index(kmask)
        return pack_score(3, tr, k1, k2, 0, 0)

    # Two pair
    pairs = uint16(ge2 & ~ge3)
    if pairs != 0 and (pairs & uint16(pairs - 1)) != 0:
        p1 = msb_index(pairs)
        pmask = uint16(pairs & ~uint16(1 << p1))
        p2 = msb_index(pmask)
        kmask = uint16(ranks & ~uint16((1 << p1) | (1 << p2)))
        k = msb_index(kmask)
        return pack_score(2, p1, p2, k, 0, 0)

    # One pair
    if pairs != 0:
        pr = msb_index(pairs)
        kmask = uint16(ranks & ~uint16(1 << pr))
        k1 = msb_index(kmask)
        if k1 >= 0:
            kmask = uint16(kmask & ~uint16(1 << k1))
        k2 = msb_index(kmask)
        if k2 >= 0:
            kmask = uint16(kmask & ~uint16(1 << k2))
        k3 = msb_index(kmask)
        return pack_score(1, pr, k1, k2, k3, 0)

    # High card (top five distinct ranks, remove the chosen MSB each time)
    r0 = msb_index(ranks)
    m2 = uint16(ranks)
    if r0 >= 0:
        m2 = uint16(m2 & ~uint16(1 << r0))
    r1 = msb_index(m2)
    if r1 >= 0:
        m2 = uint16(m2 & ~uint16(1 << r1))
    r2 = msb_index(m2)
    if r2 >= 0:
        m2 = uint16(m2 & ~uint16(1 << r2))
    r3 = msb_index(m2)
    if r3 >= 0:
        m2 = uint16(m2 & ~uint16(1 << r3))
    r4 = msb_index(m2)
    return pack_score(0, r0, r1, r2, r3, r4)


@njit(cache=True)
def evaluate(hand):
    """
    Return (cat, r0, r1, r2, r3, r4) for tie-breaking,
    using the optimized packed evaluator internally.
    """
    score = evaluate_u32(hand)
    return unpack_score(score)


@njit(cache=True, parallel=False)
def batch_evaluate_u32(hands):
    """Evaluate an array of hands shaped (N, 4) of uint16 suit-masks."""
    N = hands.shape[0]
    out = np.empty(N, dtype=np.uint32)
    for i in range(N):
        out[i] = evaluate_u32(hands[i])
    return out
