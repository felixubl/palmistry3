# palmistry3

Zero-lookup-table 7-card Texas Hold'em poker hand evaluator in C++.

Evaluates a seven-card hand to a single `uint32_t` in which a plain integer
compare ranks any two hands against each other. No lookup tables of any kind:
both the category and the kickers fall out of bit tricks on one 64-bit word, so
the evaluator's entire data footprint is zero bytes.

Roughly **260 million hands/second** on a single core (Apple M4 Pro), measured
both over the full C(52,7) enumeration and over randomly ordered hands.

Descended from [palmistry2](https://github.com/felixubl/palmistry2).

## Running it

Requires arm64: the flush test uses NEON.

```
make            # builds eval, test and bench into build/
make run        # a handful of illustrative hands
```

Or pass seven cards, rank then suit, suits being `cdhs`:

```
$ ./build/eval Ah Kh Qh Jh Th 2c 3d
Ah Kh Qh Jh Th 2c 3d     -> straight flush A raw=0x22000000

$ ./build/eval 7c 7d 5c 5d 3h 3s Ac
7c 7d 5c 5d 3h 3s Ac     -> two pair 75 + A raw=0x8051000
```

To use it as a library, include `evaluator.hpp` and hand it a `Hand`:

```cpp
using namespace pokereval;

Hand hand = 0;
add_card(hand, make_card(/*suit=*/2, /*rank=*/12));   // Ah
// ... six more

const Evaluator eval;
const Score score = eval.evaluate(hand);              // compare these directly
```

A `Card` is `suit * 16 + rank`, which is deliberately its own bit index inside
`Hand`, so building a hand is a shift and an OR per card with no division. Rank 0
is the Two and rank 12 the Ace. Feeding the evaluator a prebuilt `Hand` is the
fast path, and for simulation that varies a board against fixed hole cards, the
combination is a single OR.

## Testing

```
make test
```

There is no second implementation to differ against, so `test_exhaustive.cpp`
checks the enumeration against facts about seven-card poker that hold regardless
of implementation: the category frequencies across all 133,784,560 hands, and
the count of distinct hand values, 4824. Drop a kicker and the distinct count
falls; mis-rank a category and the histogram moves. A table of hand-versus-hand
comparisons covers the tie-breaking rules that neither number would notice, such
as the wheel being the worst straight and the third pair's rank counting as a
kicker in a three-pair hand.

The full run takes a few seconds.

## Benchmarking

```
make bench
```

Reports both the lexicographic C(52,7) walk and randomly ordered hands.
Consecutive hands in the enumeration differ by a single card, which flatters the
branch predictor, so the random figure is the one that reflects equity
simulation.

## Licence

GPL-3.0, inherited from palmistry2. See [LICENSE](LICENSE).
