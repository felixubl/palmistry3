# Lineage

This evaluator started on 16 September 2025 and has been rewritten three times
since, in three languages. Every generation is in this repository, on its own
`history/*` branch, with its original commits and dates intact. Nothing here is
built or tested any more; the branches exist so the progression can be read.

| Generation | Dates | Language | Branch |
| --- | --- | --- | --- |
| pokr | 16–19 Sep 2025 | Python | `history/pokr` |
| bb-poker-eval | 21 Sep 2025 | Python + Numba | `history/bb-poker-eval` |
| palmistry-rust | 26 Sep 2025 | Rust | `history/palmistry-rust` |
| palmistry | 8 Jan 2026 | Rust | `history/palmistry` |
| palmistry2 | 8–10 May 2026 | C++ | `history/palmistry2` |
| palmistry3 | 27 Jul 2026 – | C++ | `main` |

```
git log history/bb-poker-eval          # read a generation
git show history/palmistry-rust:src/eval_lut.rs
```

## What happened

**pokr** was a general poker sandbox, not an evaluator: hand parsing, some
matrix work, some machine learning. The evaluation problem came out of it.

**bb-poker-eval** is where the design was set, and it is the origin of
everything below. Three goals, stated in its README: find the best five-card
hand from five or more cards *without* visiting all `C(n,5)` subsets, keep
memory small, be fast. The answer was a hand held as four 13-bit rank masks,
one per suit; categories derived from rank multiplicity masks (`GE2`, `GE3`,
`GE4`) built with bitwise algebra over those four lanes; straights from an
8 KB table; and a result packed into one `uint32` so that comparing two hands
is comparing two integers. It ran at roughly 70 Mhand/s under Numba.

**palmistry-rust** is that evaluator transliterated into Rust five days later,
and it is the only generation that was never published at the time. It was
recovered from disk in September 2026 and committed with its original dates.
The tables are the same three 8192-entry tables, the hand is the same `[u16; 4]`,
and the score is packed the same way.

**palmistry** is the Rust work finished into a library: the same bitboard and
the same 8 KB of tables, plus exact and Monte Carlo equity for heads-up and
multi-way. About 25 Mhand/s, and the first version fast enough to be used for
something rather than just measured.

**palmistry2** is the move to C++, and the point where the interesting question
changed. It carries four evaluators side by side with one API and one score
format, two of them using rank tables and two using none at all, so that the
cost of the tables becomes something you can read off a benchmark instead of
argue about. The table-free ones won. That result is what the next generation
is built on.

**palmistry3** drops everything except the table-free path and rewrites it
around a different score encoding. Data footprint is still zero bytes. About
410 Mhand/s on one core of an M4 Pro taking one hand at a time, 1060 Mhand/s
when hands arrive eight at a time through a NEON batch path, and 9.3 Ghand/s
across fourteen cores.

## What carried through, and what did not

Carried through from bb-poker-eval, unchanged in substance across all three
languages:

- The hand as four per-suit rank-mask lanes in one machine word.
- Categories from rank multiplicity masks rather than subset enumeration.
- Straight detection by shifting and ANDing a rank mask.
- A single packed integer as the score, compared directly.

Replaced along the way:

- **Tables.** 32 KB in bb-poker-eval, 8 KB in the Rust versions, optional in
  palmistry2, none in palmistry3.
- **The score encoding.** bb-poker-eval packed a 4-bit category followed by
  five 4-bit rank *indices*. palmistry3 packs a category followed by two 13-bit
  rank *bitmasks*, which is what allows kicker extraction to be `m &= m - 1`.
  That encoding is not from this line of work; it is ACE_eval's, and the
  README's prior-art section credits it along with everything else here that
  was arrived at elsewhere first.

## The archived repositories

Each generation was its own repository. They are archived read-only rather than
deleted, so existing links still resolve:

- [pokr](https://github.com/felixubl/pokr)
- [bb-poker-eval](https://github.com/felixubl/bb-poker-eval)
- [palmistry](https://github.com/felixubl/palmistry)
- [palmistry2](https://github.com/felixubl/palmistry2)

palmistry2 also has a `dev` branch, not merged and not imported here, that cuts
it down to the single fastest evaluator. palmistry3 supersedes it.
