# Lineage

This evaluator started on 16 September 2025 and has been rewritten several times
since, in four languages. Every generation is in this repository under
[`history/`](history), in order, with its original commits and dates intact —
`git log` here reaches back to the first one. Nothing under `history/` is built
or tested any more; it is kept so the progression can be read.

| Generation | Dates | Language | Directory |
| --- | --- | --- | --- |
| pokr | 16–19 Sep 2025 | Python | [`history/1-pokr`](history/1-pokr) |
| bb-poker-eval | 21–22 Sep 2025 | Python + Numba | [`history/2-bb-poker-eval`](history/2-bb-poker-eval) |
| palmistry-rust | 26 Sep 2025 | Rust | [`history/3-palmistry-rust`](history/3-palmistry-rust) |
| palmistry-eval | 26–27 Sep 2025 | Rust, Julia, C++ | [`history/4-palmistry-eval`](history/4-palmistry-eval) |
| ceval | 27–28 Sep 2025 | C++, Python | [`history/5-ceval`](history/5-ceval) |
| palmistry | 8 Jan 2026 | Rust | [`history/6-palmistry`](history/6-palmistry) |
| palmistry2 | 8–10 May 2026 | C++ | [`history/7-palmistry2`](history/7-palmistry2) |
| palmistry3 | 27 Jul 2026 – | C++ | the repository root |

```
git log -- history/2-bb-poker-eval      # one generation's commits
git log --reverse --date=short --pretty='%ad %s' | head
```

## What happened

**pokr** was a general poker sandbox, not an evaluator: hand parsing, some
matrix work, some machine learning. The evaluation problem came out of it.

**bb-poker-eval** is where the design was set, and it is the origin of
everything after it. Three goals, stated in its README: find the best five-card
hand from five or more cards *without* visiting all `C(n,5)` subsets, keep
memory small, be fast. The answer was a hand held as four 13-bit rank masks,
one per suit; categories derived from rank multiplicity masks (`GE2`, `GE3`,
`GE4`) built with bitwise algebra over those four lanes; straights from an
8 KB table; and a result packed into one `uint32` so that comparing two hands
is comparing two integers. It ran at roughly 70 Mhand/s under Numba.

Then came four days in late September that decided everything after them. The
three generations below are one continuous piece of work, and none of it was
committed at the time; it was recovered from disk a year later, in September
2026, and the commits carry their original timestamps.

**palmistry-rust** is the evaluator transliterated into Rust on the evening of
26 September. The tables are the same three 8192-entry tables, the hand is the
same `[u16; 4]`, and the score is packed the same way.

**palmistry-eval** starts eleven minutes after the last edit to palmistry-rust
and is the bake-off. The same evaluator gets written in Rust, then Julia, then
C++ three times over, PokerStove is cloned to be measured against, and an equity
calculator falls out along the way. **This is where the C++ work begins — 27
September 2025, not the following May.** By the end of it the core has been
extracted into a library-shaped file with no benchmark or RNG attached, and two
real bugs have been found and fixed: `pack_score` was shifting rank fields in
unmasked, so a negative rank could corrupt the ordering of an entire score, and
`uniform()` had a comment claiming rejection sampling above a plain modulo.

Neither the Julia measurements nor the PokerStove comparison were ever written
down, and the Julia benchmark decodes deck indices with the card-int scheme
rather than the deck scheme, so its hands are malformed and its throughput was
never comparable to the C++ figures. The Rust crate in that generation does not
compile: `py.rs` imports two modules that do not exist and `card.rs` is truncated
mid-edit, with a rank convention that contradicts the evaluator's. It is kept as
it was rather than repaired. The vendored PokerStove checkout and the compiled
binaries are not kept.

**ceval** begins that same evening and runs to half past two in the morning. The
evaluator is split behind a header with its internals made static, benchmarking
is pulled out into a reusable module, the equity calculator is rewritten as an
exhaustive one with a specialisation per street, and the Numba port is set up as
a uv project. It ends somewhere else entirely: an 800-line genetic-algorithm
poker bot, a heads-up no-limit engine and a feed-forward network trained over 500
generations, finishing with a completed run whose evolved opening ranges were
saved to disk. That bot is not an evaluator, so it lives in its own repository at
[pokerevo](https://github.com/felixubl/pokerevo) rather than here.

(There is also a `ceval7` directory from the same afternoon, created and
abandoned 84 seconds later. It is empty, so there is nothing to keep.)

**palmistry** is the Rust work finished into a library: the same bitboard and
the same 8 KB of tables, plus exact and Monte Carlo equity for heads-up and
multi-way. About 25 Mhand/s, and the first version fast enough to be used for
something rather than just measured.

**palmistry2** returns to C++ after four months away, and is the point where the
interesting question changed. It carries four evaluators side by side with one
API and one score format, two of them using rank tables and two using none at
all, so that the cost of the tables becomes something you can read off a
benchmark instead of argue about. The table-free ones won. That result is what
the current evaluator is built on.

**palmistry3**, at the repository root, drops everything except the table-free
path and rewrites it around a different score encoding. Data footprint is zero
bytes. About 410 Mhand/s on one core of an M4 Pro taking one hand at a time,
1060 Mhand/s when hands arrive eight at a time through a NEON batch path, and
9.3 Ghand/s across fourteen cores.

## What carried through, and what did not

Carried through from bb-poker-eval, unchanged in substance across all four
languages:

- The hand as four per-suit rank-mask lanes in one machine word.
- Categories from rank multiplicity masks rather than subset enumeration.
- Straight detection by shifting and ANDing a rank mask.
- A single packed integer as the score, compared directly.

Replaced along the way:

- **Tables.** 32 KB in bb-poker-eval and everything through September 2025,
  8 KB in the finished Rust library, optional in palmistry2, none now.
- **The score encoding.** bb-poker-eval packed a 4-bit category followed by
  five 4-bit rank *indices*, and that survived unchanged for ten months. The
  current one packs a category followed by two 13-bit rank *bitmasks*, which is
  what allows kicker extraction to be `m &= m - 1`. That encoding is not from
  this line of work; it is ACE_eval's, and the README's prior-art section credits
  it along with everything else here that was arrived at elsewhere first.

## The archived repositories

Four of these generations were once their own repository. They are archived
read-only rather than deleted, so existing links still resolve, and their
contents are here under `history/`:

- [pokr](https://github.com/felixubl/pokr)
- [bb-poker-eval](https://github.com/felixubl/bb-poker-eval)
- [palmistry](https://github.com/felixubl/palmistry)
- [palmistry2](https://github.com/felixubl/palmistry2)

palmistry2 also has a `dev` branch, not merged and not kept here, that cuts it
down to the single fastest evaluator. The current evaluator supersedes it.

palmistry-rust, palmistry-eval and ceval were never repositories at all.
