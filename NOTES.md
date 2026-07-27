# palmistry3

## Commands

```
make            # eval, test and bench into build/
make test       # exhaustive correctness, takes a few seconds
make bench      # throughput, both workloads
```

## One evaluator

This repo holds exactly one evaluator, and it is the fastest known version. Do
not add a second implementation alongside it, do not keep an old one for
comparison, and do not add a portability fallback path unless asked. If a change
is faster, it replaces what is there. Benchmark variants belong in a scratch
directory, not in the tree.

## Constraints that are not obvious from the code

**arm64 only.** `flush_info` uses NEON intrinsics from `<arm_neon.h>`. This is
deliberate: scalar `__builtin_popcount` on arm64 compiles to an
fmov/cnt/uaddlv/fmov round trip through the vector unit, and the four suit lanes
need four of them, whereas one `cnt` + `uaddlp` gets every lane at once. A
portable SWAR fallback exists and measured about 10% slower, so it was left out
rather than carried. CI runs on `macos-latest` for this reason.

**Score fields are rank bitmasks, not rank indices.** The packing is
`category << 26 | primary << 13 | secondary`, where primary and secondary are
13-bit masks. This works because within any one category both fields always hold
a fixed number of bits, and for two masks of equal popcount, integer order is
exactly lexicographic order of their descending rank lists. It is what lets
kicker extraction be `m &= m - 1` instead of a serial chain of five `clz` steps,
which is worth about 15% on its own. Any change that makes a field's popcount
vary within a category silently breaks ordering, and the exhaustive test will
catch it via the distinct-value count.

**The common-category tail is deliberately branchless and shared.** Trips, two
pair, one pair and high card are 89.6% of hands and are computed by one
straight-line block with no data-dependent branch. Splitting it back into four
branchy cases costs real throughput. Conversely, do not push this further: a
fully branchless evaluator that computes a candidate for all nine categories and
takes the max was measured at less than half the speed. The branches for the
rare categories are well predicted and close to free.

## Changing the evaluator

`make test` is the safety net and it is a strong one, so run it on every change
to `evaluator.hpp` rather than reasoning about correctness. If the histogram
matches and the distinct count is still 4824, the change is almost certainly
sound; if either moves, it is definitely broken.

When claiming a speedup, quote the random-hands number, not the C(52,7) one.
Consecutive hands in the enumeration differ by one card and the branch predictor
exploits that, so the enumeration overstates performance for anything
branch-sensitive.
