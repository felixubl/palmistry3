# palmistry3

Zero-lookup-table 7-card Texas Hold'em poker hand evaluator in C++.

Most fast poker evaluators are fast because they precompute. They build a table,
sometimes a very large one, and turn evaluation into a handful of array reads.
This one has no table at all. Its entire data footprint is zero bytes. A hand
lives in a single 64-bit integer, and both its category and its kickers are
extracted by bit manipulation on that one word.

It evaluates about **285 million hands per second** on one core of an Apple M4
Pro, which as far as I can measure makes it the fastest table-free 7-card
evaluator available. The techniques it uses are mostly not mine. See
[Prior art](#prior-art) at the bottom, which is the most important section in
this file.

## The numbers

Apple M4 Pro, evaluating a prebuilt `Hand` unless stated otherwise:

| | Mhand/s | ns/hand | workload |
|---|---|---|---|
| Evaluation only | 285 | 3.51 | 2M random hands, 1 core, best of 11 |
| Including building the hand from cards | 214 | 4.68 | 2M random hands, 1 core, best of 11 |
| Lexicographic C(52,7) enumeration | 292 | 3.43 | all 133,784,560 hands, 1 core, best of 3 |
| Evaluation only, 8 threads | 1759 | 0.57 | 2M random hands, 8 cores, best of 5 |

Quote the first row or the second. The enumeration figure walks hands in
lexicographic order, so consecutive hands differ by a single card and the branch
predictor gets an easy ride. It is the number everyone publishes and the number
that means least.

## Quick start

Requires arm64. The flush test uses NEON.

```
make            # builds eval, test and bench into build/
make run        # a handful of illustrative hands
make test       # exhaustive correctness, a few seconds
make bench      # both workloads
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
way is to loop over seven cards and tally. Instead, keep a running answer and
fold in one suit at a time.

Think of it as four voters, one per suit, each voting for every rank it holds.
You are not interested in exact totals, only in four thresholds: seen at least
once, twice, three times, four times. A rank already sitting in "at least once"
that gets another vote moves up to "at least twice".

```cpp
uint16_t ones = s0;
uint16_t twos = ones & s1;   ones |= s1;
uint16_t threes = twos & s2; twos |= ones & s2; ones |= s2;
const uint16_t quads = threes & s3;
threes |= twos & s3;         twos |= ones & s3; ones |= s3;
```

Thirteen ranks are resolved at once, in about thirteen operations, and no card is
ever looked at individually. `ones` is every rank present, `twos` every pair or
better, `threes` every trip or better, `quads` the four-of-a-kind.

### Straights without a lookup

Write the ranks you hold as a strip of tape with a hole punched at each one. Lay
four more copies of the tape on top, each slid one position further along. Where
light passes through all five layers, five consecutive ranks are present.

```cpp
const uint16_t a = mask & (mask >> 1);   // two in a row
const uint16_t b = a & (a >> 2);         // four in a row
const uint16_t starts = b & (mask >> 4); // five in a row
```

`starts` has a bit at the *lowest* rank of every run, so the highest such bit
plus four is the best straight's top card. The wheel (A-2-3-4-5) is not a
contiguous run, because the ace sits at the top of the strip rather than the
bottom, so it gets its own one-line test.

### Flushes in a single pass

A flush needs five cards in one suit, so you need the population count of each of
the four lanes. Asking for four scalar popcounts is surprisingly expensive on
arm64: each `__builtin_popcount` compiles to an `fmov`/`cnt`/`uaddlv`/`fmov`
round trip through the vector unit, and the compiler chains all four behind
branches. Since 97% of hands are not flushes, almost every hand paid for all
four.

One NEON `cnt` plus `uaddlp` counts all four lanes at once. Then the "at least
five" test is pure integer arithmetic, done on all four lanes simultaneously:

```cpp
const uint64_t hit = (counts + 0x0003000300030003ull) & 0x0008000800080008ull;
```

A count of five or more is exactly a count whose value plus three has bit 3 set.
Seven cards means no lane can exceed seven, so plus three can never reach sixteen
and spill into the neighbouring lane. Those three spare bits per lane are paying
for themselves.

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

### One block for nine tenths of all hands

Trips, two pair, one pair and high card are 89.6% of all hands. Written the
obvious way that is four branches and four different kicker routines. But once
straight flush, quads, full house, flush and straight have been ruled out, those
four categories are the same shape: some ranks you hold multiples of, then
kickers, then nothing. Written in terms of the masks above they turn out to be
the same code.

```cpp
const uint16_t pair_lo = drop_low(twos);
const bool three_pairs = drop_low(pair_lo) != 0;
const uint16_t primary = three_pairs ? pair_lo : twos;
const uint16_t rest = ones ^ primary;
const uint16_t trimmed = drop_low(rest);
const uint16_t secondary = three_pairs ? trimmed : drop_low(trimmed);
const uint32_t category = (twos != 0) + (pair_lo != 0) + 2 * (threes != 0);
return pack_score(category, primary, secondary);
```

The category falls out arithmetically. One pair sets the first term. Two pair
sets the first two. Trips sets the first and the last. High card sets none of
them and lands on zero.

The only wrinkle is three pairs, which is possible with seven cards. You keep the
better two, and the third pair's rank stays in the running as an ordinary kicker,
so one fewer card gets dropped. That is the `three_pairs` select.

No data-dependent branch, no `clz`, and nine tenths of all hands leave through
it.

### What is deliberately *not* branchless

The obvious next step is to go fully branchless: compute a candidate score for
all nine categories and take the maximum. I tried it. It runs at **less than half
the speed**. The branches for the rare categories are predicted almost perfectly
because they are rare, so they cost close to nothing, while computing eight
scores you throw away costs a great deal. The cheap branches stay.

## Performance, honestly

Compared against the two most relevant evaluators, rebuilt and re-run on the same
machine, on the same hands, in the same harness, alternating which runs first so
neither gets the warm cache:

| against the other table-free evaluator | Mhand/s | table size |
|---|---|---|
| palmistry3 | 283 | 0 B |
| [ACE_eval](https://github.com/ashelly/ACE_eval) | 217 | 0 B |

| against a table-based evaluator | Mhand/s | table size |
|---|---|---|
| palmistry3 | 279 | 0 B |
| [OMPEval](https://github.com/zekyll/OMPEval) | 1622 | ~190 KB |

Each pair was measured together in one process, which is why palmistry3's own
figure differs slightly between them. Comparing numbers across separate runs is
how you end up believing things that are not true, so the two comparisons are
kept apart.

Both were checked for agreement and not just speed: zero category disagreements
against ACE and zero ordering disagreements against OMPEval, across 2M hands
each.

Two things worth taking from that table.

**Against the other table-free evaluator, this one is 1.30x faster.** ACE's own
published figure is 72 Mhps, but that was measured on a 2012 mobile i5, so it
needed re-running to mean anything.

**Against a good small table, this one loses badly, by 5.8x.** OMPEval's tables
are only about 190KB, which sits comfortably in L2, so its lookups are nearly
free. No amount of bit twiddling competes with that. If you want the fastest
possible evaluator and do not care about the table, use OMPEval.

The case for zero tables is narrower and worth stating precisely. The famous
[TwoPlusTwo evaluator](https://github.com/tangentforks/TwoPlusTwoHandEvaluator)
uses a 123MB table, and on OMPEval's published cross-project benchmark it runs at
1588 Mhand/s on lexicographic enumeration and **19 Mhand/s on random hands**, an
84x collapse, because a 123MB table cannot stay in cache under random access.
palmistry3 holds its ~285 Mhand/s regardless of access pattern, because there is
no working set to miss. That is the niche: workloads with no locality, and
environments where a table is inconvenient.

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
is branch-free by computing all categories and taking a maximum, which is the
approach measured at half speed above.

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
  an AND/OR tree, ACE uses a 2-bit SWAR add. The saturating accumulate spelled
  out above is a bit-slice counter, a textbook idiom outside poker (see
  [chessprogramming on SWAR](https://www.chessprogramming.org/SIMD_and_SWAR_Techniques)).
- [Cactus Kev](http://suffe.cool/poker/evaluator.html) uses a 13-bit rank mask,
  but as a table index rather than as an ordered score, which is a different
  idea that is easy to confuse with this one.

Two things I have looked for and **not** found prior art for, which is weaker
evidence than finding it:

1. Deriving the category arithmetically as
   `(twos != 0) + (pair_lo != 0) + 2 * (threes != 0)`, which folds trips into the
   same straight-line block. ACE reaches the same tail through an `else if` chain
   and handles trips separately.
2. The NEON `cnt` + `uaddlp` lowering of the suit count. The SWAR equivalent is
   girving's, from 2011.

Descended from [palmistry2](https://github.com/felixubl/palmistry2).

## Licence

GPL-3.0, inherited from palmistry2. See [LICENSE](LICENSE).
