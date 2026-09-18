# bench_eval.jl
# Benchmark the evaluate_u32() routine (hands per second)

using Random
using BenchmarkTools
using Printf: @sprintf 
include("poker_eval.jl")  # brings in evaluate_u32(hand::Hand)

# --- small adapter so we can store hands as isbits tuples (no allocation per hand)
const HandTuple = NTuple{4,UInt16}

@inline evaluate_u32(h::HandTuple)::UInt32 = begin
    # same body as evaluate_u32(Hand) but reading tuple fields (no allocations)
    h0, h1, h2, h3 = h
    ranks = (h0 | h1 | h2 | h3) & MASK13
    ge4   = (h0 & h1 & h2 & h3) & MASK13
    ge2   = ((h0 & h1) | (h0 & h2) | (h0 & h3) | (h1 & h2) | (h1 & h3) | (h2 & h3)) & MASK13
    ge3   = ((h0 & h1 & h2) | (h0 & h1 & h3) | (h0 & h2 & h3) | (h1 & h2 & h3)) & MASK13

    # Straight flush
    best_sf = -1
    if popcnt13(h0) >= 5
        se = straight_end_from_mask(h0); if se > best_sf; best_sf = se; end
    end
    if popcnt13(h1) >= 5
        se = straight_end_from_mask(h1); if se > best_sf; best_sf = se; end
    end
    if popcnt13(h2) >= 5
        se = straight_end_from_mask(h2); if se > best_sf; best_sf = se; end
    end
    if popcnt13(h3) >= 5
        se = straight_end_from_mask(h3); if se > best_sf; best_sf = se; end
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
    if popcnt13(h0) >= 5; m = h0
    elseif popcnt13(h1) >= 5; m = h1
    elseif popcnt13(h2) >= 5; m = h2
    elseif popcnt13(h3) >= 5; m = h3
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
        if k1 >= 0; kmask &= ~(UInt16(1) << UInt16(k1)); end
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
    r1 = msb_index(m2); if r1 >= 0; m2 &= ~(UInt16(1) << UInt16(r1)); end
    r2 = msb_index(m2); if r2 >= 0; m2 &= ~(UInt16(1) << UInt16(r2)); end
    r3 = msb_index(m2); if r3 >= 0; m2 &= ~(UInt16(1) << UInt16(r3)); end
    r4 = msb_index(m2)
    return pack_score(0, r0, r1, r2, r3, r4)
end

# --- random hand generator (bitboards only; outside hot loop) -----------------
const DECK_SUITS = ntuple(i->UInt16(1), 4)  # just a placeholder

# Ranks: 0..12, Suits: 0..3. We’ll sample 7 unique cards and fold into four 13-bit masks.
function rand_hand_tuple!(rng::AbstractRNG)::HandTuple
    # draw 7 unique cards via reservoir-ish sampling (fast, no big allocations)
    seen = UInt64(0)
    h0 = h1 = h2 = h3 = UInt16(0)
    n = 0
    while n < 7
        c = rand(rng, 0:51)
        bit = UInt64(1) << c
        if (seen & bit) == 0
            seen |= bit
            suit = c >> 4            # 0..3
            rank = c & 0xF           # 0..12
            rbit = UInt16(1) << UInt16(rank)
            if suit == 0; h0 |= rbit
            elseif suit == 1; h1 |= rbit
            elseif suit == 2; h2 |= rbit
            else; h3 |= rbit
            end
            n += 1
        end
    end
    return (h0 & MASK13, h1 & MASK13, h2 & MASK13, h3 & MASK13)
end

function make_hands(N::Int; seed::Int=0)
    rng = MersenneTwister(seed)
    v = Vector{HandTuple}(undef, N)
    @inbounds for i in 1:N
        v[i] = rand_hand_tuple!(rng)
    end
    return v
end

# --- throughput helpers -------------------------------------------------------
# report MH/s given a BenchmarkTools Trial and the number of evals done
mhz(trial, N) = (N / (minimum(trial).time / 1e9)) / 1e6

# --- main ---------------------------------------------------------------------
function main()
    N = parse(Int, get(ARGS, 1, "2000000"))  # number of hands
    println("Generating $N random 7-card hands...")
    hands = make_hands(N; seed=123)

    println("\nSingle-threaded benchmark (@btime over pre-generated hands):")
    s = Ref{UInt32}(0)
    trial = @benchmark begin
        x = UInt32(0)
        @inbounds for h in $hands
            x ⊻= evaluate_u32(h)   # XOR to keep compiler honest
        end
        $s[] = x
    end
    show(trial); println()
    println(@sprintf("Throughput: %.2f million hands/s", mhz(trial, length(hands))))

    # Multi-threaded pass: simple manual timing
    println("\nMulti-threaded pass (Threads.@threads):")
    Threads.nthreads() > 1 || println("Note: JULIA_NUM_THREADS not set >1; set it to use multiple cores.")
    t = @elapsed begin
        partial = Vector{UInt32}(undef, Threads.nthreads())
        Threads.@threads for tid in 1:Threads.nthreads()
            # each thread processes a contiguous chunk
            nt = Threads.nthreads()
            n = length(hands)
            chunk = (tid-1)*cld(n, nt) + 1 : min(tid*cld(n, nt), n)
            acc = UInt32(0)
            @inbounds for i in chunk
                acc ⊻= evaluate_u32(hands[i])
            end
            partial[tid] = acc
        end
        # fold (keep side-effect)
        s[] = foldl(xor, partial; init=UInt32(0))
    end
    println(@sprintf("Elapsed: %.3f s | Throughput: %.2f million hands/s",
                     t, (length(hands)/t)/1e6))
end

main()
