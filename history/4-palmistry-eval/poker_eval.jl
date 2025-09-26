# poker_eval.jl — Julia port of the provided Rust snippet

# Types ------------------------------------------------------------------------
const Hand = Vector{UInt16}  # length-4 vector of 13-bit suit bitmasks

# Constants --------------------------------------------------------------------
const MASK13      = UInt16((1 << 13) - 1)
const WHEEL_MASK  = UInt16((1 << 12) | (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3))

# RUN5: 9 masks of consecutive 5-bit runs inside a 13-bit rank mask
const RUN5 = let a = Vector{UInt16}(undef, 9)
    for i in 0:8
        a[i+1] = UInt16(((UInt16(1) << 5) - UInt16(1)) << i) & MASK13
    end
    a
end

# --- LUTs (8192 entries for 13-bit masks) -------------------------------------
# Note: Julia arrays are 1-based; index with (mask + 1)

const POPCNT13 = let t = Vector{UInt8}(undef, 8192)
    for m in 0:8191
        t[m+1] = UInt8(count_ones(UInt16(m)))
    end
    t
end

const HIBIT13 = let t = Vector{Int8}(undef, 8192)
    t[1] = Int8(-1)  # for mask == 0
    for m in 1:8191
        # floor(log2(m)) for 16-bit; 15 - leading_zeros
        t[m+1] = Int8(15 - leading_zeros(UInt16(m)))
    end
    t
end

const STRAIGHT_END13 = let t = Vector{Int16}(undef, 8192)
    for m in 0:8191
        mm = UInt16(m)
        end_ = Int16(-1)
        for s in 8:-1:0
            w = RUN5[s+1]
            if (mm & w) == w
                end_ = Int16(s + 4)
                break
            end
        end
        if end_ < 3 && ((mm & WHEEL_MASK) == WHEEL_MASK)
            end_ = Int16(3)  # A-2-3-4-5 (wheel) ends at 5 (rank 3 with 0-based ranks)
        end
        t[m+1] = end_
    end
    t
end

# --- small helpers -------------------------------------------------------------

@inline function card_to_int(suit::Integer, rank::Integer)::Int32
    Int32((Int32(suit) << 4) | Int32(rank))
end

@inline function decode_card(card::Integer)::Tuple{Int32,Int32}
    rank = Int32(card) & 0xF
    suit = (Int32(card) >> 4) & 0x3
    return (suit, rank)
end

@inline function empty_hand()::Hand
    Hand(fill(UInt16(0), 4))
end

"Set the card bit. Returns 0 if newly added, 1 if already present."
function add_card!(hand::Hand, card::Integer)::Int32
    suit, rank = decode_card(card)
    bit = UInt16(1) << UInt16(rank)
    old = hand[suit + 1]  # suits indexed 0..3 in data, 1..4 in Julia
    if (old & bit) != 0
        return Int32(1)
    end
    hand[suit + 1] = (old | bit) & MASK13
    return Int32(0)
end

@inline popcnt13(mask::UInt16)::Int32 = Int32(POPCNT13[Int(mask) + 1])

@inline straight_end_from_mask(mask::UInt16)::Int32 = Int32(STRAIGHT_END13[Int(mask) + 1])

@inline msb_index(mask::UInt16)::Int32 = Int32(HIBIT13[Int(mask) + 1])

"Return the top five ranks (hi..lo) present in mask; fill with -1 if fewer."
function top5_from_mask(m::UInt16)
    out = fill(Int32(-1), 5)
    i = 1
    mm = m
    while i <= 5
        r = msb_index(mm)
        out[i] = r
        if r < 0
            break
        end
        mm &= ~(UInt16(1) << UInt16(r))
        i += 1
    end
    return (out[1], out[2], out[3], out[4], out[5])
end

"Pack cat + 5 tiebreakers into UInt32 (higher is better)."
@inline function pack_score(cat::Integer, r0::Integer, r1::Integer, r2::Integer, r3::Integer, r4::Integer)::UInt32
    (UInt32(cat & 0xF) << 20) |
    (UInt32(r0  & 0xF) << 16) |
    (UInt32(r1  & 0xF) << 12) |
    (UInt32(r2  & 0xF) <<  8) |
    (UInt32(r3  & 0xF) <<  4) |
    UInt32(r4  & 0xF)
end

"Optional compatibility helper."
function unpack_score(s::UInt32)
    si = Int32(s)
    cat = (si >> 20) & 0xF
    r0  = (si >> 16) & 0xF
    r1  = (si >> 12) & 0xF
    r2  = (si >>  8) & 0xF
    r3  = (si >>  4) & 0xF
    r4  =  si        & 0xF
    return (cat, r0, r1, r2, r3, r4)
end

"Evaluate 7 cards in bitboard form and return a packed UInt32 strength."
function evaluate_u32(hand::Hand)::UInt32
    h0, h1, h2, h3 = hand[1], hand[2], hand[3], hand[4]

    ranks = (h0 | h1 | h2 | h3) & MASK13
    ge4   = (h0 & h1 & h2 & h3) & MASK13
    ge2   = ((h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)) & MASK13
    ge3   = ((h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)) & MASK13

    # Straight flush
    best_sf = -1
    if popcnt13(h0) >= 5
        se = straight_end_from_mask(h0)
        if se > best_sf; best_sf = se; end
    end
    if popcnt13(h1) >= 5
        se = straight_end_from_mask(h1)
        if se > best_sf; best_sf = se; end
    end
    if popcnt13(h2) >= 5
        se = straight_end_from_mask(h2)
        if se > best_sf; best_sf = se; end
    end
    if popcnt13(h3) >= 5
        se = straight_end_from_mask(h3)
        if se > best_sf; best_sf = se; end
    end
    if best_sf >= 0
        return pack_score(8, best_sf, 0, 0, 0, 0)
    end

    # Four of a kind
    all4 = ge4
    if all4 != 0
        qr = msb_index(all4)
        kmask = (ranks & ~(UInt16(1) << UInt16(qr))) & MASK13
        kr = msb_index(kmask)
        return pack_score(7, qr, kr, 0, 0, 0)
    end

    # Full house
    exactly3 = ge3 & ~ge4 & MASK13
    if exactly3 != 0
        tr1 = msb_index(exactly3)
        pairs_only = ge2 & ~ge3 & MASK13
        pmask = (pairs_only & ~(UInt16(1) << UInt16(tr1))) & MASK13
        pr = msb_index(pmask)
        if pr >= 0
            return pack_score(6, tr1, pr, 0, 0, 0)
        end
        tr2mask = (exactly3 & ~(UInt16(1) << UInt16(tr1))) & MASK13
        tr2 = msb_index(tr2mask)
        if tr2 >= 0
            return pack_score(6, tr1, tr2, 0, 0, 0)
        end
    end

    # Flush (top-5 in-suit)
    m = UInt16(0)
    if popcnt13(h0) >= 5
        m = h0
    elseif popcnt13(h1) >= 5
        m = h1
    elseif popcnt13(h2) >= 5
        m = h2
    elseif popcnt13(h3) >= 5
        m = h3
    end
    if m != 0
        r0, r1, r2, r3, r4 = top5_from_mask(m)
        return pack_score(5, r0, r1, r2, r3, r4)
    end

    # Straight
    se = straight_end_from_mask(ranks)
    if se >= 0
        return pack_score(4, se, 0, 0, 0, 0)
    end

    # Three of a kind
    trips = ge3 & ~ge4 & MASK13
    if trips != 0
        tr = msb_index(trips)
        kmask = (ranks & ~(UInt16(1) << UInt16(tr))) & MASK13
        k1 = msb_index(kmask)
        if k1 >= 0
            kmask &= ~(UInt16(1) << UInt16(k1))
        end
        k2 = msb_index(kmask)
        return pack_score(3, tr, k1, k2, 0, 0)
    end

    # Two pair
    pairs = ge2 & ~ge3 & MASK13
    if pairs != 0 && (pairs & (pairs - UInt16(1))) != 0
        p1 = msb_index(pairs)
        pmask = (pairs & ~(UInt16(1) << UInt16(p1))) & MASK13
        p2 = msb_index(pmask)
        kmask = (ranks & ~((UInt16(1) << UInt16(p1)) | (UInt16(1) << UInt16(p2)))) & MASK13
        k = msb_index(kmask)
        return pack_score(2, p1, p2, k, 0, 0)
    end

    # One pair
    if pairs != 0
        pr = msb_index(pairs)
        kmask = (ranks & ~(UInt16(1) << UInt16(pr))) & MASK13
        k1 = msb_index(kmask)
        if k1 >= 0; kmask &= ~(UInt16(1) << UInt16(k1)); end
        k2 = msb_index(kmask)
        if k2 >= 0; kmask &= ~(UInt16(1) << UInt16(k2)); end
        k3 = msb_index(kmask)
        return pack_score(1, pr, k1, k2, k3, 0)
    end

    # High card
    r0 = msb_index(ranks)
    m2 = ranks & ~(UInt16(1) << UInt16(max(r0, 0)))
    r1 = msb_index(m2)
    if r1 >= 0; m2 &= ~(UInt16(1) << UInt16(r1)); end
    r2 = msb_index(m2)
    if r2 >= 0; m2 &= ~(UInt16(1) << UInt16(r2)); end
    r3 = msb_index(m2)
    if r3 >= 0; m2 &= ~(UInt16(1) << UInt16(r3)); end
    r4 = msb_index(m2)
    return pack_score(0, r0, r1, r2, r3, r4)
end

# --- (Optional) tiny demo -----------------------------------------------------
#=
# Example: build a 7-card hand (Ah Kh Qh Jh Th 9c 2d) and evaluate.
h = empty_hand()
for (suit, rank) in [(0,12),(0,11),(0,10),(0,9),(0,8),(1,7),(2,0)]
    add_card!(h, card_to_int(suit, rank))
end
strength = evaluate_u32(h)
println("Packed score = 0x", hex(strength))
println("Unpacked     = ", unpack_score(strength))
=#
