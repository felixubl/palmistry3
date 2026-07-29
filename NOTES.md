# palmistry3

## Commands

```
make            # eval, test and bench into build/
make test       # exhaustive correctness, takes a few seconds
make bench      # throughput, both workloads, both entry points
```

## One evaluator, two widths

The target caller is an equity simulator, so there are two entry points:

- `evaluate(Hand)`, one hand, scalar.
- `evaluate_batch(const Hand*, Score*, size_t)`, eight hands per iteration in
  NEON, with a scalar tail for any remainder under eight. About 2.5x the
  per-hand throughput of the scalar path, and the one to use if hands arrive in
  bulk.

They are two spellings of **one algorithm**, not two implementations, and that is
the rule to hold: any change to the category cascade or the kicker packing has to
land in both. Nothing enforces this by construction, so `make test` enforces it by
measurement: the exhaustive enumeration runs through `evaluate_batch` and
cross-checks every one of the 133,784,560 scores against `evaluate`, then the
histogram and the distinct count vouch for both at once. If the two ever drift,
the "batch vs scalar" line goes non-zero before anything else does.

Beyond those two, do not add a third implementation, do not keep an old one for
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

**The common-category tail is deliberately branchless and shared.** Two pair, one
pair and high card are 84.7% of hands and are computed by one straight-line block
with no data-dependent branch. Splitting it back into three branchy cases costs
real throughput. Conversely, do not push this further: a fully branchless
evaluator that computes a candidate for all nine categories and takes the max was
measured at less than half the speed. The branches for the rare categories are
well predicted and close to free.

**Trips is resolved in the multiples block, not in the tail, and that asymmetry
is deliberate.** The outer test on `threes` is rare and well predicted, so it is a
branch. The inner "full house or only bare trips" question splits about two to
one, which is the one test in the evaluator whose direction the predictor cannot
learn, so it is paid for with selects and the block returns unconditionally. The
consequence to preserve: because trips cannot reach the tail, the tail's category
is `(twos != 0) + (pair_lo != 0)` with no `threes` term.

**The flush block returns early, and that is a correctness argument, not just an
ordering choice.** With seven cards a flush suit takes five of them, leaving two,
so a flush hand can never also be quads (which needs three cards outside the
flush suit) or a full house (which needs three across its trip and its pair). That
is what allows the whole suited half of the cascade to be settled by one branch
before `multiplicity` is even computed. If that argument ever stops holding, the
early return silently mis-ranks hands.

**`msb16` is undefined at zero** now that it is `clz`, and every caller reaches it
having already branched on its argument being non-zero. **`straight_bit` relies on
bit 0 of its widened 14-bit frame staying free** for the `| 1` sentinel that keeps
its own `clz` defined; that bit is the repeated ace, which can never top a run.

**`evaluate` is `[[gnu::always_inline]]` for a measured reason.** Clang declines
to inline it at `-O3` because the body is over its size budget, and then every
call pays a `bl`, a `ret`, a `this` it never reads, and a 64-bit literal that
should have been hoisted out of the caller's loop.

**The batch path's transpose is free, and that is why it is worth having.** Vector
code needs one register per suit holding eight hands, which is the transpose of
one word per hand holding four suits. `vld4q_u16` de-interleaves on the way in, so
it lands suit s of hand h in `val[s]` lane h at no extra cost. This works only
because a hand's four suit lanes are contiguous 16-bit words, which is a
consequence of the 16-bit lane stride chosen so a card's code is its own bit
index. Change the stride or the lane order and the batch path loses its reason to
exist.

**Branchless is a liability scalar and an asset batched, and both facts are
measured.** Computing a candidate for all nine categories and selecting was under
half speed scalar, because the wasted work serves one hand. The batch kernel has
no choice but to do exactly that, since eight lanes disagree about their category,
and there the same work serves eight hands. Do not "fix" the batch kernel by
making it branchy per lane, and do not port its select chain back to the scalar
path.

**Per-batch guards pay at low frequencies, not middling ones.** A whole batch can
branch even though a lane cannot. The flush block is guarded by one `vmaxvq` over
the suit counts and is worth about 13%, because a flush is 3.06% of hands so
0.969^8 = 78% of batches skip it and the predictor gets that right nearly four
times in five. The same guard over the multiples block measured **27% slower**:
trips or better is 7.6%, so 0.924^8 = 53% skip, which is a coin flip, and a coin
flip is the one thing a branch predictor can do nothing with. Straights (4.62%,
so 68/32) are in the same unhelpful region. Do not add more guards without
checking which side of that line the frequency falls on.

**The scalar path is mispredict-bound, not issue-bound, so do not trade a branch
for instructions.** This is the most useful thing to know before optimising it
further, and it is counter-intuitive enough that it was measured the hard way:
seven separate attempts to cut the hot path by merging the rare tests behind one
disjunction, or by making the straight exit a real branch instead of a select, all
reduced the static instruction count (56 down to as low as 48) and all came out
**slower on random hands**, from -0.8% to -22%. The worst offenders were exactly
the ones that merged the most branches. Meanwhile several of them made the C(52,7)
*enumeration* faster, because lexicographic order makes those same branches
predictable, which is a clean demonstration of why the random-hands number is the
one to quote. Reducing instruction count is only worth it while the branch profile
stays put.

**Measured and rejected, do not re-propose without a new angle:** computing
`multiplicity` in NEON rather than integer ops (-7.6%, the three results have to
come back across the register-file boundary); `rbit`/`neg`/`and`/`rbit` for the
highest set bit instead of `clz` (-2.1%);
`__builtin_expect_with_probability` on the rare branches (+0.2%, noise);
splitting the straight into predicate plus extractor so only real straights pay
the `clz` (-3.5%, seven instructions cheaper and still slower); merging the flush,
trips and straight tests into one branch (-22%); building the tail's two kicker
chains side by side to halve its dependency depth (-5.0%, which is what says
throughput-bound rather than latency-bound).

## Changing the evaluator

`make test` is the safety net and it is a strong one, so run it on every change
to `evaluator.hpp` rather than reasoning about correctness. If the histogram
matches and the distinct count is still 4824, the change is almost certainly
sound; if either moves, it is definitely broken.

When claiming a speedup, quote the random-hands number, not the C(52,7) one.
Consecutive hands in the enumeration differ by one card and the branch predictor
exploits that, so the enumeration overstates performance for anything
branch-sensitive.
