# poker_eval

Fast Texas Hold'em hand evaluation and equity calculation.

## Performance

**Evaluation:** 40-45 ns/hand (~25M hands/sec, single-threaded)

**Exact equity:**
- River: 94ns
- Turn: 1.5μs
- Flop: 32μs
- Preflop (HU): 35ms

**Monte Carlo (heads-up preflop):**
- 1k iterations: 28μs
- 10k iterations: 394μs
- 100k iterations: 4.3ms
- 1M iterations: 43ms

**Monte Carlo multi-way (10k iterations):**
- 3-way: 646μs
- 6-way: 971μs

## Usage

```rust
use poker_eval::{Card, parse_hole_cards, parse_board, evaluate_u32, BitBoard4x13};

let hero = parse_hole_cards("AsKh")?;
let board = parse_board("Qh Jh Th")?;

let mut bb = BitBoard4x13::new();
bb.add_id(hero[0].id());
bb.add_id(hero[1].id());
for c in &board {
    bb.add_id(c.id());
}

let score = evaluate_u32(&bb);
```

### Equity Calculation

```rust
use poker_eval::{equity_mc_vs_hand_checked, equity_exact_vs_hand_checked};

let aces = [0, 13];  // As, Ad
let kings = [1, 14]; // Ks, Kd
let flop = [26, 27, 28]; // 2h, 3h, 4h

// Monte Carlo (10k iterations)
let eq = equity_mc_vs_hand_checked(&aces, &kings, &flop, 10_000, 42)?;

// Exact enumeration
let eq = equity_exact_vs_hand_checked(&aces, &kings, &flop)?;
```

## API

**Evaluation:**
- `evaluate_u32(&BitBoard4x13) -> Score`
- `evaluate_u32_from_ids(&[u8]) -> Score`

**Equity (heads-up):**
- `equity_mc_vs_hand_checked` - Monte Carlo vs known hand
- `equity_mc_vs_random_checked` - Monte Carlo vs random hand
- `equity_exact_vs_hand_checked` - Exact vs known hand
- `compare_showdown_checked` - Compare on complete board

**Equity (multi-way, 3-9 players):**
- `equity_mc_multiway_checked` - All known hands
- `equity_mc_vs_random_multiway_checked` - Hero vs N-1 random
- `equity_exact_multiway_checked` - Exact enumeration

**Parsing:**
- `Card::from_str("As")` / `card.to_string()`
- `parse_hole_cards("As Kh")` or `parse_hole_cards("AsKh")`
- `parse_board("Ah Kh Qh")`

## Card Encoding

Cards use ID encoding 0-51:
- ID = suit * 13 + rank
- Clubs: 0-12, Diamonds: 13-25, Hearts: 26-38, Spades: 39-51

## Examples

```bash
cargo run --release --example equity_bench
```

## Implementation

- 4×13 bitboard (one u16 per suit)
- 8KB lookup tables (popcount, hibit, straight detection)
- XorShift64 PRNG for Monte Carlo
- Specialized nested loops for exact enumeration

## License

MIT OR Apache-2.0
