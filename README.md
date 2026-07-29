# palmistry3

Zero-lookup-table 7-card Texas Hold'em poker hand evaluator in C++.

Most fast poker evaluators are fast because they precompute. They build a table,
sometimes a very large one, and turn evaluation into a handful of array reads.
This one has no table at all. Its entire data footprint is zero bytes. A hand
lives in a single 64-bit integer, and both its category and its kickers are
extracted by bit manipulation on that one word.

It evaluates about **410 million hands per second** one hand at a time on one core
of an Apple M4 Pro, about **1060 million per second** when hands arrive in bulk,
through an eight-wide NEON path that produces bit-identical scores, and about
**9.3 billion per second** across all fourteen cores. As far as I can measure that
makes it the fastest table-free 7-card evaluator available.

The techniques it uses are mostly not mine. See [Prior art](#prior-art) at the
bottom, which is the most important section in this file.

## The numbers

Apple M4 Pro, one core, evaluating prebuilt `Hand`s:

| | Mhand/s | ns/hand | workload |
|---|---|---|---|
| `evaluate`, one hand at a time | 409 | 2.44 | 2M random hands, best of 9 x 7 runs |
| `evaluate_batch`, eight at a time | 1062 | 0.94 | 2M random hands, best of 9 x 7 runs |
| Lexicographic C(52,7), scalar | 449 | 2.23 | all 133,784,560 hands, best of 3 x 7 runs |
| Lexicographic C(52,7), batch | 784 | 1.28 | all 133,784,560 hands, best of 3 x 7 runs |

> **Trust ratios over absolutes.** The same binary on the same idle machine drifts
> by around 6% between sittings, presumably thermal: the immediately preceding
> version of this evaluator measured 285.6 Mhand/s early in one session and 268.3
> late in it, with identical code. So every speedup claim here comes from running
> the binaries alternately within one sitting, never from comparing numbers taken
> at different times. Measured that way, against the previous version:
> **1.53x** for the scalar path and **3.88x** for the batch path.
>
> One older row, hand-building included (was 214), was dropped rather than carried
> forward, having not been re-measured.

Quote one of the first two rows, and say which. The enumeration figures walk hands
in lexicographic order, so consecutive hands differ by a single card and the branch
predictor gets an easy ride; that is the number everyone publishes and the number
that means least. Note the batch path gains *less* on the enumeration than on
random hands, which is the same effect seen from the other side: the scalar path
has branches to be flattered, and the batch path has fewer to flatter.

### Across cores

`make bench-threads`, same machine, 10 performance cores and 4 efficiency cores.
This is a deliberately harsher harness than the table above: 16M hands is 128 MB,
far too much for cache, so it measures a streaming workload rather than a warm one.
That is why one thread reads 898 here against 1062 above, and it is the more honest
number for a simulation that will not fit in L2.

| threads | scalar | batch | | |
|---|---|---|---|---|
| 1 | 382 | 898 | | |
| 4 | 1509 | 3565 | 3.97x | |
| 8 | 2970 | 6969 | 7.82x | all performance cores |
| 14 | 3928 | **9336** | 10.39x | performance + efficiency |

Mhand/s. So about **9.3 billion hands a second** on one M4 Pro, 0.107 ns/hand.

Scaling is essentially perfect to 8 threads, then depends entirely on how the work
is handed out, and that choice is worth more than several rounds of instruction
tuning:

| batch, 14 threads | Mhand/s |
|---|---|
| even split across threads | 6932 |
| chunks from an atomic cursor | 9336 |

An even split gives an efficiency core the same share as a performance core and
then waits for it, so the whole run finishes at efficiency-core speed and adding
those four cores makes things **worse** than the 8005 an even split gets from 12.
Handing out chunks lets each core take what it can, and scaling becomes monotonic.
Both schedulers are checked against each other's checksum in the benchmark, because
a scheduler that quietly dropped work would simply look faster.

## Quick start

Requires arm64. The flush test and the batch path use NEON.

```
make            # builds eval, test and both benches into build/
make run        # a handful of illustrative hands
make test       # exhaustive correctness, a few seconds
make bench      # both workloads, both entry points
make bench-threads   # thread scaling, both entry points, both schedulers
```

Pass seven cards as rank then suit, suits being `cdhs`:

```
$ ./build/eval Ah Kh Qh Jh Th 2c 3d
Ah Kh Qh Jh Th 2c 3d     -> straight flush A raw=0x22000000

$ ./build/eval 7c 7d 5c 5d 3h 3s Ac
7c 7d 5c 5d 3h 3s Ac     -> two pair 75 + A raw=0x8051000
```

As a library, include `evaluator.hpp` and hand it a `Hand`:

```cpp
using namespace pokereval;

Hand hand = 0;
add_card(hand, make_card(/*suit=*/2, /*rank=*/12));   // Ah
// ... six more

const Evaluator eval;
const Score score = eval.evaluate(hand);   // compare these with <, >, ==
```

Scores are directly comparable. A higher `Score` is a better hand, and equal
scores are genuine ties. There is nothing to unpack before comparing.

## How it works

### A hand is one 64-bit word

Four lanes of 16 bits, one per suit. Within a lane, bit *r* is set if you hold
that rank in that suit. Thirteen ranks need thirteen bits, so three bits per lane
go unused, and that slack is what makes the arithmetic below work.

```
 bits 63..48    bits 47..32    bits 31..16    bits 15..0
   spades         hearts        diamonds        clubs
 ...A K Q J .   ...A K Q J .   ...A K Q J .   ...A K Q J .
```

A `Card` is `suit * 16 + rank`, which is deliberately the card's own bit index in
that word. So adding a card is one shift and one OR, with no division and no
table:

```cpp
inline Hand card_bit(Card card) { return Hand(1) << card; }
```

### Counting ranks without counting anything

You need to know which ranks appear twice, three times, four times. The obvious
way is to loop over seven cards and tally.

Think of it as four voters, one per suit, each voting for every rank it holds.
You are not interested in exact totals, only in four thresholds: seen at least
once, twice, three times, four times. Those four thresholds are the four
*symmetric* functions of four inputs, which means they form a sorting network,
and a network is a tree rather than a chain. So pair the voters up instead of
folding them in one at a time:

```cpp
a = s0 & s1;  b = s0 | s1;   // both of the first pair, either of the first pair
c = s2 & s3;  d = s2 | s3;

ones = b | d;                quads = a & c;
twos = (a | c) | (b & d);    threes = (a | c) & (b & d);
```

Read `a` as "at least two of {s0,s1}" and `b` as "at least one of {s0,s1}". Then
two copies overall means either one pair doubled up (`a | c`) or one from each
pair (`b & d`), and three copies means both of those at once. `twos` and `threes`
sharing their two operands is what makes this cheap: they are just `u | v` and
`u & v`.

The lanes never have to be pulled out separately either. Pairing (s0,s1) and
(s2,s3) is a shift by 16, and pairing the pairs is a shift by 32, so the whole
thing runs on the undivided 64-bit word:

```cpp
const Hand p = hand & (hand >> 16);   // lane 0 holds a, lane 2 holds c
const Hand q = hand | (hand >> 16);   // lane 0 holds b, lane 2 holds d
```

arm64 folds a shifted second operand into a logical op for free, so both of those
are one instruction. Thirteen ranks are resolved at once, in nine operations of
dependency depth four, and no card is ever looked at individually.

### Straights without a lookup

Write the ranks you hold as a strip of tape with a hole punched at each one. Lay
four more copies of the tape on top, each slid one position further along. Where
light passes through all five layers, five consecutive ranks are present.

The wheel (A-2-3-4-5) is the awkward case, because the ace sits at the top of the
strip rather than the bottom. Rather than give it its own test, punch a second
hole for the ace *below* the two, in a 14-bit frame where rank r sits at bit r+1
and the ace is repeated at bit 0. Now the wheel is an ordinary run like any other.

```cpp
const uint32_t m = uint32_t(mask) << 1 | uint32_t(mask >> 12);
const uint32_t a = m & (m >> 1);        // two in a row
const uint32_t b = a & (a >> 2);        // four in a row
const uint32_t tops = (m & (b << 4)) | 1u;
```

`tops` marks each run at its *highest* rank, which costs no more than marking the
lowest, and it pays for itself twice over. It leaves bit 0 free, because a run can
never be topped by the repeated ace, and a spare bit 0 is somewhere to put a
sentinel. With the `| 1` in place the highest set bit can be taken with `clz`,
which is two instructions and would otherwise need a guard for the no-straight
case, instead of a four-step shift-or smear.

### Flushes in a single pass

A flush needs five cards in one suit, so you need the population count of each of
the four lanes. Asking for four scalar popcounts is surprisingly expensive on
arm64: each `__builtin_popcount` compiles to an `fmov`/`cnt`/`uaddlv`/`fmov`
round trip through the vector unit, and the compiler chains all four behind
branches. Since 97% of hands are not flushes, almost every hand paid for all
four.

One NEON `cnt` plus `uaddlp` counts all four lanes at once. The "at least five"
test then stays in the vector unit as well, because the counts are already sitting
there and one `cmhs` against a splat of five is cheaper than moving them out:

```cpp
const uint16x4_t counts = vpaddl_u8(vcnt_u8(v));
const uint64_t hit = vget_lane_u64(
    vreinterpret_u64_u16(vcge_u16(counts, vdup_n_u16(5))), 0);
```

That is all the hot path does. *Which* suit flushed, what ranks it holds and how
many of them there are cost a `ctz` and two shifts that 96.9% of hands have no use
for, so they live inside the flush branch rather than in front of it. Left in
front, the compiler hoists them and every hand pays for six instructions that
almost no hand reads. A hit lane also comes back all-ones rather than as a single
bit part-way up the lane, which puts `ctz` exactly on the lane boundary and saves
realigning it.

### The score is a number that already sorts correctly

This is the idea the whole design rests on.

A score has to answer "which hand wins", so the natural encoding is a category
followed by the tie-breaking ranks in descending order. Store those ranks as
numbers and comparing two hands means walking the list until they differ.
Extracting them means five rounds of count-leading-zeros, each one waiting on the
last, which is slow.

Store the ranks as a **bitmask** instead and the comparison becomes free. Suppose
two hands come down to their top five cards:

```
        A  K  Q  J  T  9  8  7  6  5  4  3  2
A K 9 5 2:  1  1  0  0  0  1  0  0  0  1  0  0  1   = 6281
A K 9 6 4:  1  1  0  0  0  1  0  0  1  0  1  0  0   = 6292
```

6292 beats 6281, and the six beating the five is exactly why. This is not a
coincidence. Comparing two binary numbers means finding the highest bit where
they differ, and comparing two poker hands means finding the highest rank where
they differ. They are the same operation. As long as both masks hold the same
number of bits, integer order *is* poker order.

So a score packs a category and two rank masks:

```
category << 26 | primary << 13 | secondary
```

| category | primary | secondary |
|---|---|---|
| straight flush | top card | |
| quads | quad rank | kicker |
| full house | trip rank | pair rank |
| flush | top 5 ranks | |
| straight | top card | |
| trips | trip rank | 2 kickers |
| two pair | 2 pair ranks | 1 kicker |
| one pair | pair rank | 3 kickers |
| high card | | top 5 ranks |

Every field holds a fixed number of bits for its category, so the property holds
everywhere. And trimming a mask to its top few ranks stops being a loop and
becomes the oldest trick in the book, clearing the lowest set bit:

```cpp
inline uint16_t drop_low(uint16_t mask) { return mask & (mask - 1); }
```

### One block for five sixths of all hands

Two pair, one pair and high card are 84.7% of all hands. Written the obvious way
that is three branches and three different kicker routines. But once straight
flush, quads, full house, flush, straight and trips have been ruled out, those
three categories are the same shape: some ranks you hold multiples of, then
kickers, then nothing. Written in terms of the masks above they turn out to be the
same code.

```cpp
const uint16_t pair_lo = drop_low(twos);
const bool three_pairs = drop_low(pair_lo) != 0;
const uint16_t primary = three_pairs ? pair_lo : twos;
const uint16_t rest = ones ^ primary;
const uint16_t trimmed = drop_low(rest);
const uint16_t secondary = trimmed & (trimmed - !three_pairs);
const uint32_t category = (twos != 0) + (pair_lo != 0);
return pack_score(category, primary, secondary);
```

The category falls out arithmetically, and it is not really an independent
quantity: it is the popcount of `primary`. One pair sets the first term, two pair
sets both, high card sets neither and lands on zero.

The only wrinkle is three pairs, which is possible with seven cards. You keep the
better two, and the third pair's rank stays in the running as an ordinary kicker,
so one fewer card gets dropped. That is what the `three_pairs` select and the
conditional subtrahend on the last drop are for. Spelling the second drop as
`trimmed - !three_pairs` rather than as a second ternary reads the flag once into
a 0/1 register instead of twice into two selects.

No data-dependent branch, and five sixths of all hands leave through it.

### Trips leave one step earlier, on purpose

Trips used to come through that block too, which is what the old
`2 * (threes != 0)` term in the category was for. It no longer does, and the
reason is a branch that cannot be predicted.

Quads and a full house both need three of a rank, so one test gates the whole
multiples block, and 92.4% of hands skip it. But *inside* that block, "is this a
full house or only trips" splits about two to one, which is the one test in the
evaluator whose direction the predictor has no way to learn. So the block resolves
both with selects and returns unconditionally rather than dropping bare trips out
of the bottom. Bare trips means `twos` holds the trip rank alone, so `primary` is
the trip rank either way and only the category and the kickers differ.

That has a pleasant knock-on effect. Trips can no longer reach the common tail, so
the tail's category loses a term and its two remaining terms are exactly
`popcount(primary)`.

### What is deliberately *not* branchless

The obvious next step is to go fully branchless: compute a candidate score for
all nine categories and take the maximum. I tried it *one hand at a time*. It runs
at **less than half the speed**. (That is not a criticism of the designs that do
this, such as girving's. They are vector designs, where it is the right answer, as
the next section works out.) The branches for the rare categories are predicted almost perfectly
because they are rare, so they cost close to nothing, while computing eight
scores you throw away costs a great deal. The cheap branches stay.

Hold onto that result, because the next section turns it upside down.

### Eight hands at once, where branchless becomes the fast way

`evaluate_batch` runs the same evaluator eight hands wide in NEON and is about 2.5x
faster per hand. Nothing about the poker logic changes: every step is bitwise work
on 13-bit rank masks, and an AND does not care whether the register holds one hand
or eight side by side. Three things make that pay rather than merely work.

**The transpose is free.** Vector code wants one register holding the clubs of
eight different hands, another the diamonds, and so on. That is the transpose of
what a `Hand` is, one word holding four suits, and a transpose normally costs a
fistful of shuffles, which is exactly the overhead that makes a lot of
plausible-looking vectorisation not worth doing. Here it costs nothing.
`vld4q_u16` is a four-way *de-interleaving* load: it reads 32 consecutive `uint16`
and deals every fourth one to the same register, like dealing cards. Eight
consecutive hands in memory *are* 32 consecutive `uint16`, and every fourth one is
the same suit. So the load already is the transpose.

That only falls out because a hand's four suit lanes are contiguous 16-bit words,
which was chosen so that a card's code is its own bit index. The batch path is
collecting a dividend on a decision made for an unrelated reason.

**The rejected design becomes the winning one.** Eight hands in one register
disagree about their category, so there is nothing to branch on: the cascade has to
become a chain of `bsl` selects that computes a candidate for every category. That
is precisely the design measured at less than half speed above. But scalar, that
wasted work serves one hand; here it serves eight. The arithmetic that sank it is
what makes it win. Going wide does not just make the same algorithm faster, it
changes which algorithm is best.

**Branches come back, per batch instead of per hand.** A lane cannot branch, but
eight lanes can agree that none of them needs the rare work, and a horizontal max
is cheap. A flush is 3.06% of hands, so 0.969⁸ = 78% of batches contain no flush at
all and skip 37 instructions of flush handling. Worth about 13%.

The frequency matters more than it looks. The same trick over the multiples block
is not a smaller win, it is a 27% **loss**: trips or better is 7.6% of hands, so
0.924⁸ = 53% of batches skip, which is a coin flip, and a coin flip is the one
thing a branch predictor can do nothing with. Per-batch guards pay at low
frequencies and hurt at middling ones.

Getting the flush work into one skippable block needs it moved to the *end* of the
select chain, after quads and the full house. That is legal for the same reason the
scalar path can return early on a flush: seven cards cannot hold both.

The two paths are one algorithm, so `make test` cross-checks every one of the
133,784,560 scores from the batch path against the scalar path, and only then
counts the histogram. They agree exactly, including the scalar fallback that
handles any remainder under eight.

## Performance, honestly

All four evaluators below were cloned, rebuilt and run in **one process, on the same
hands, alternating**, best of 5. Every evaluator gets its own preferred prebuilt
representation, built before the clock starts, in its own contiguous array. 128k
hands, so nothing is measuring memory bandwidth. Built with `-flto`, because ACE and
phevaluator live in separate translation units and would otherwise pay a real call
per hand while the two header-inline evaluators did not.

Reproduce with `compare/run.sh`, which clones all three, builds them and runs the
harness. It is not a `make` target because it needs the network.

| evaluator | Mhand/s | ns/hand | table |
|---|---|---|---|
| [OMPEval](https://github.com/zekyll/OMPEval) | 1560 | 0.64 | ~190 KB |
| **palmistry3, `evaluate_batch`** | **1018** | 0.98 | 0 B |
| **palmistry3, `evaluate`** | **420** | 2.38 | 0 B |
| [ACE_eval](https://github.com/ashelly/ACE_eval) | 207 | 4.84 | 0 B |
| [phevaluator](https://github.com/HenryRLee/PokerHandEvaluator) | 91 | 11.0 | ~150 KB |

Agreement was checked, not assumed: zero category disagreements against ACE over
131,072 hands, and zero *ordering* disagreements against all three over 4M random
hand pairs. Ordering rather than equality is the right test, since all four encode
their scores differently and only the induced order has to match. A fast wrong
answer is not a result.

Each rival gets its own best entry point. ACE ships six variants and its README
names `ace_eval_decompress.c` as the fastest; that is also the table-free one, and
it measured 3.4x its siblings here (207 against 56 and 60), so the choice is not
close. phevaluator exposes one 7-card function.

Three things worth taking from that table.

**Against the other table-free evaluator, this one is 2.0x faster one hand at a
time and 4.9x faster in bulk.** That was 1.30x before the current round of work.
ACE's own published figure is 72 Mhps, but that was measured on a 2012 mobile i5,
so it needed re-running to mean anything.

**Against a good small table, this one still loses, but by 1.5x rather than 5.8x.**
That is the honest headline. OMPEval's tables are only about 190KB, which sits
comfortably in L2, so its lookups are nearly free, and no amount of bit twiddling
competes with two cached loads. What changed is the size of the gap: 5.8x is a
different category of thing from 1.5x. If you want the fastest possible evaluator
and do not care about carrying a table, still use OMPEval.

Two caveats in both directions. The batch path needs hands in blocks of eight, so a
caller with one hand in hand faces 3.7x rather than 1.5x. But OMPEval has no batch
API at all, and nothing here suggests its shape would gain one as cheaply: two
dependent table lookups per hand do not vectorise the way arithmetic does.

**A table-based evaluator is not automatically the faster one.** phevaluator carries
a table and is 4.6x *slower* than the table-free path here, and 11x slower than the
batch path. Its 7-card entry point takes seven separate card ids and hashes them, so
it pays per card where the others pay per hand. Carrying a table only helps if the
table is doing the work.

The case for zero tables is narrower than the top row and worth stating precisely.
The famous [TwoPlusTwo evaluator](https://github.com/tangentforks/TwoPlusTwoHandEvaluator)
uses a 123MB table, and on OMPEval's published cross-project benchmark it runs at
1588 Mhand/s on lexicographic enumeration and **19 Mhand/s on random hands**, an
84x collapse, because a 123MB table cannot stay in cache under random access. This
evaluator has no working set to miss, so its random-hands figure is its only figure.
That is the niche: workloads with no locality, and environments where a table is
inconvenient. The narrowed gap to OMPEval makes that niche a better bargain than it
was, not a different one.

## Correctness

There is no second implementation to differ against, so `make test` checks the
enumeration against facts about seven-card poker that hold regardless of how you
compute them:

- the exact category frequencies over all 133,784,560 hands
- the number of distinct hand values, 4824

Between them those pin down both the category cascade and the kicker packing.
Drop a kicker and the distinct count falls. Mis-rank a category and the histogram
moves. A table of hand-versus-hand comparisons then covers the tie-breaking rules
that neither number would notice, such as the wheel being the worst straight, and
the third pair's rank counting as a kicker in a three-pair hand.

The bitmask score encoding was additionally verified to be **order-isomorphic** to
a conventional rank-index score across all 133,784,560 hands, meaning every
pairwise comparison between any two hands gives an identical result.

There are two entry points and only one algorithm, so the enumeration is driven
through the eight-wide `evaluate_batch` and **every one of the 133,784,560 scores is
cross-checked against the scalar `evaluate`** before the histogram counts it. That
makes the histogram and the distinct count vouch for both paths at once, and it
means the two cannot quietly drift apart: the "batch vs scalar" line goes non-zero
first. Every remainder length from 0 to 40 is checked separately, since a batch
whose size is not a multiple of eight finishes on the scalar path.

The histogram and the distinct count are strong but not complete: a permutation of
scores *within* one category that preserved every field's popcount would move
neither number. So when the evaluator was last restructured, the new version was
also diffed hand for hand against the old one over all 133,784,560 hands and found
bit-identical. That check is not in `make test`, because it needs two evaluators in
the tree and there is only ever one, but it is the check worth reaching for after
any change large enough that the histogram alone would not reassure you.

## Prior art

Nearly every technique here has been invented before, in several cases more than
once, and it would be wrong to present any of it as new. What follows is as
accurate as I have been able to establish.

**[ACE_eval](https://github.com/ashelly/ACE_eval) by ashelly, which its own
README traces back to a
[StackOverflow code-golf answer](https://stackoverflow.com/a/3392025) from August
2010, is the closest ancestor.** Its README describes a 32-bit score laid out as
`RRRR..AKQJT98765432akqjt98765432`, that is a 4-bit category followed by 13 value
bits and 13 kicker bits, "arranged so that if V(a) > V(b) then hand a beats hand
b". That is the same score encoding used here. It also already contains the
shared branchless kicker tail across trips, two pair, one pair and high card,
including the `pl = twos & (twos-1)` step, the three-pairs test, and the
conditional extra bit drop. If you find the ideas in this README interesting, ACE
is where they came from.

**[girving/poker](https://github.com/girving/poker) by Geoffrey Irving (2011)**
independently arrived at the four-suit-lane 64-bit representation, the same
`u & u>>2 & unique>>3` straight detection, bitmask score fields, and all four
suit popcounts in a single SWAR pass with a branchless "at least five" test. It
is branch-free by computing all categories and taking a maximum.

**girving also got to batching across hands first, and the batch path here is his
design, not a new one.** His evaluator is OpenCL and its working type is `ulong4`,
where each 64-bit lane holds one entire hand with the four suits at bit offsets
0/13/26/39 inside it. The two axes are orthogonal: bit offsets carry suits, vector
lanes carry hands. Verified from source at `06fa71d`, where the SWAR masks are
declared as the *scalar* `cards_t` and then combined with vector operands, which is
only coherent if every lane independently holds all four suits. So the combination
that matters, table-free plus data-parallel across hands plus a branchless select
chain in place of the cascade, is his, in 2011.

Two things differ, and only the second is a claim.

He needs no transpose at all, because he never spends lanes on suits. A hand is 52
bits and a lane is 64, so a whole hand fits in one lane and batching is just N
hands in N lanes, which an ordinary contiguous `vload4` already gives you. OpenCL C
has no de-interleaving load primitive, so he could not have used one. This
evaluator spends lanes on hands *per suit*, four `uint16x8_t` registers of eight
hands each, which does need a transpose and gets it free from `vld4q_u16`. The
payoff is density, eight hands per 128-bit register against two, and no masking
between suit fields, since separate registers cannot leak bits the way adjacent
13-bit fields inside one lane can. Using a de-interleaving load to get
array-of-structs to struct-of-arrays for free is a well-known general SIMD
technique and is not claimed here either; what I have not found is it being used
this way in a card evaluator.

What he does *not* do is guard a rare block on a whole-batch test. There is no
`any(`/`all(` anywhere in his evaluator, every `max` is component-wise rather than
horizontal, and his own pre-branchless ancestor's `if (flushes) { ... }` was deleted
outright when it was vectorised rather than converted into a lane-population test,
so the wasted work is never clawed back. The `vmaxvq` guard over the flush block
here, and the finding that such a guard pays at 3% frequency and costs 27% at 7.6%,
is the one part of the batch path I have not found prior art for.

Other independent arrivals at the bitmask-comparable score:
[MrKWatkins](https://www.mrkwatkins.co.uk/evaluating-poker-hands/) (2022), who
documents the identical "secondary rank mask, primary rank mask, hand type"
layout, and [ngoc](https://blog.ngoc.io/posts/stacking-chips) (2025).

Further credits:

- The four-suit-lane representation appears in Steve Brecher's HandEval and in
  Andrew Prock's [PokerStove](https://github.com/andrewprock/pokerstove), among
  others. The 16-bit lane stride specifically is used by MrKWatkins and ngoc.
- Detecting a run by ANDing shifted copies of a bitboard predates poker work
  entirely. The canonical reference is John Tromp's
  [Fhourstones](https://en.wikipedia.org/wiki/Fhourstones) Connect-4 solver
  (1996).
- Computing "appears at least N times" by pure bitwise operations on suit lanes
  is standard. PokerStove uses direct symmetric-function formulas, girving uses
  an AND/OR tree, ACE uses a 2-bit SWAR add. This evaluator used to use a
  bit-slice counter, a textbook idiom outside poker (see
  [chessprogramming on SWAR](https://www.chessprogramming.org/SIMD_and_SWAR_Techniques)),
  and folding it into the pair tree above moved it onto the same ground as
  girving and PokerStove. The tree is theirs, not mine. What I have not found
  elsewhere is doing it without extracting the lanes at all, by pairing them with
  a shift by 16 and the pairs with a shift by 32 so the whole tree runs on the
  undivided 64-bit word.
- [Cactus Kev](http://suffe.cool/poker/evaluator.html) uses a 13-bit rank mask,
  but as a table index rather than as an ordered score, which is a different
  idea that is easy to confuse with this one.

Two things I have looked for and **not** found prior art for, which is weaker
evidence than finding it:

1. Deriving the category arithmetically as `(twos != 0) + (pair_lo != 0)`, so
   that it is the popcount of the primary field rather than a separate decision.
   This claim used to be stronger: the term `2 * (threes != 0)` folded trips into
   the same straight-line block, and ACE by contrast reaches the same tail through
   an `else if` chain and handles trips separately. Trips is now resolved
   separately here too, for the branch-prediction reason given above, which moves
   this evaluator's *structure* closer to ACE's and leaves only the arithmetic
   category itself as the part I have not found elsewhere.
2. The NEON `cnt` + `uaddlp` lowering of the suit count. The SWAR equivalent is
   girving's, from 2011.

Descended from [palmistry2](https://github.com/felixubl/palmistry2).

## Licence

GPL-3.0, inherited from palmistry2. See [LICENSE](LICENSE).
