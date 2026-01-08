//! Benchmark equity calculations to measure performance across different scenarios.
//!
//! Usage:
//!   cargo run --release --example equity_bench
//!
//! This measures:
//! - Heads-up equity (2 players)
//! - Multi-way equity (3-9 players)
//! - Exact vs Monte Carlo comparison
//! - Different board states (preflop, flop, turn, river)

use std::time::Instant;
use poker_eval::{
    equity_exact_vs_hand_checked,
    equity_mc_vs_hand_checked,
    equity_mc_vs_random_checked,
    equity_mc_multiway_checked,
    equity_mc_vs_random_multiway_checked,
    equity_exact_multiway_checked,
    Card, Rank::*, Suit::*,
};

fn format_duration(nanos: u128) -> String {
    if nanos < 1_000 {
        format!("{} ns", nanos)
    } else if nanos < 1_000_000 {
        format!("{:.1} μs", nanos as f64 / 1_000.0)
    } else if nanos < 1_000_000_000 {
        format!("{:.2} ms", nanos as f64 / 1_000_000.0)
    } else {
        format!("{:.3} s", nanos as f64 / 1_000_000_000.0)
    }
}

fn bench<F>(name: &str, iterations: u64, mut f: F)
where
    F: FnMut(),
{
    // Warmup
    for _ in 0..10 {
        f();
    }

    let start = Instant::now();
    for _ in 0..iterations {
        f();
    }
    let duration = start.elapsed();

    let total_ns = duration.as_nanos();
    let per_iter_ns = total_ns / iterations as u128;
    let per_sec = (iterations as f64) / duration.as_secs_f64();

    println!("{:50} {:>12}  ({:>10.0} /s)",
        name,
        format_duration(per_iter_ns),
        per_sec
    );
}

fn main() {
    println!("=== Equity Calculator Benchmarks ===\n");
    println!("{:50} {:>12}  {:>13}", "Scenario", "Time/Iter", "Throughput");
    println!("{:-<78}", "");

    // Setup test hands
    let aces = [Card::new(Spades, Ace).id(), Card::new(Hearts, Ace).id()];
    let kings = [Card::new(Spades, King).id(), Card::new(Hearts, King).id()];
    let queens = [Card::new(Spades, Queen).id(), Card::new(Hearts, Queen).id()];
    let jacks = [Card::new(Spades, Jack).id(), Card::new(Hearts, Jack).id()];

    let flop = [
        Card::new(Clubs, King).id(),
        Card::new(Diamonds, Queen).id(),
        Card::new(Hearts, Two).id(),
    ];

    let turn = [
        Card::new(Clubs, King).id(),
        Card::new(Diamonds, Queen).id(),
        Card::new(Hearts, Two).id(),
        Card::new(Spades, Three).id(),
    ];

    println!("\n--- Heads-Up Exact Equity ---");

    bench("HU Exact: River (complete board)", 1000, || {
        let board = [
            Card::new(Clubs, King).id(),
            Card::new(Diamonds, Queen).id(),
            Card::new(Hearts, Two).id(),
            Card::new(Spades, Three).id(),
            Card::new(Clubs, Four).id(),
        ];
        let _ = equity_exact_vs_hand_checked(&aces, &kings, &board).unwrap();
    });

    bench("HU Exact: Turn (4 board cards)", 1000, || {
        let _ = equity_exact_vs_hand_checked(&aces, &kings, &turn).unwrap();
    });

    bench("HU Exact: Flop (3 board cards)", 100, || {
        let _ = equity_exact_vs_hand_checked(&aces, &kings, &flop).unwrap();
    });

    bench("HU Exact: Preflop (0 board cards)", 10, || {
        let _ = equity_exact_vs_hand_checked(&aces, &kings, &[]).unwrap();
    });

    println!("\n--- Heads-Up Monte Carlo (10k iterations) ---");

    bench("HU MC 10k: Preflop", 100, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &[], 10_000, 42).unwrap();
    });

    bench("HU MC 10k: Flop", 100, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &flop, 10_000, 42).unwrap();
    });

    bench("HU MC 10k: Turn", 100, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &turn, 10_000, 42).unwrap();
    });

    println!("\n--- Heads-Up vs Random Opponent ---");

    bench("HU vs Random MC 10k: Preflop", 100, || {
        let _ = equity_mc_vs_random_checked(&aces, &[], 10_000, 42).unwrap();
    });

    bench("HU vs Random MC 10k: Flop", 100, || {
        let _ = equity_mc_vs_random_checked(&aces, &flop, 10_000, 42).unwrap();
    });

    println!("\n--- Multi-Way Exact Equity ---");

    bench("3-way Exact: Turn", 100, || {
        let _ = equity_exact_multiway_checked(&[&aces, &kings, &queens], &turn).unwrap();
    });

    bench("3-way Exact: Flop", 10, || {
        let _ = equity_exact_multiway_checked(&[&aces, &kings, &queens], &flop).unwrap();
    });

    bench("3-way Exact: Preflop", 1, || {
        let _ = equity_exact_multiway_checked(&[&aces, &kings, &queens], &[]).unwrap();
    });

    bench("6-way Exact: Turn", 10, || {
        let tens = [Card::new(Clubs, Ten).id(), Card::new(Diamonds, Ten).id()];
        let nines = [Card::new(Clubs, Nine).id(), Card::new(Diamonds, Nine).id()];
        let _eights = [Card::new(Clubs, Eight).id(), Card::new(Diamonds, Eight).id()];
        let _ = equity_exact_multiway_checked(
            &[&aces, &kings, &queens, &jacks, &tens, &nines],
            &turn
        ).unwrap();
    });

    println!("\n--- Multi-Way Monte Carlo (10k iterations) ---");

    bench("3-way MC 10k: Preflop", 50, || {
        let _ = equity_mc_multiway_checked(&[&aces, &kings, &queens], &[], 10_000, 42).unwrap();
    });

    bench("3-way MC 10k: Flop", 50, || {
        let _ = equity_mc_multiway_checked(&[&aces, &kings, &queens], &flop, 10_000, 42).unwrap();
    });

    bench("6-way MC 10k: Preflop", 50, || {
        let tens = [Card::new(Clubs, Ten).id(), Card::new(Diamonds, Ten).id()];
        let nines = [Card::new(Clubs, Nine).id(), Card::new(Diamonds, Nine).id()];
        let _eights = [Card::new(Clubs, Eight).id(), Card::new(Diamonds, Eight).id()];
        let _ = equity_mc_multiway_checked(
            &[&aces, &kings, &queens, &jacks, &tens, &nines],
            &[],
            10_000,
            42
        ).unwrap();
    });

    println!("\n--- Hero vs N Random Opponents (10k iterations) ---");

    bench("Hero vs 1 random (HU): Preflop", 100, || {
        let _ = equity_mc_vs_random_multiway_checked(&aces, 1, &[], 10_000, 42).unwrap();
    });

    bench("Hero vs 2 random (3-way): Preflop", 100, || {
        let _ = equity_mc_vs_random_multiway_checked(&aces, 2, &[], 10_000, 42).unwrap();
    });

    bench("Hero vs 5 random (6-max): Preflop", 100, || {
        let _ = equity_mc_vs_random_multiway_checked(&aces, 5, &[], 10_000, 42).unwrap();
    });

    bench("Hero vs 8 random (9-max): Preflop", 100, || {
        let _ = equity_mc_vs_random_multiway_checked(&aces, 8, &[], 10_000, 42).unwrap();
    });

    println!("\n--- MC Iteration Scaling (Preflop HU) ---");

    bench("MC 1k iterations", 1000, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &[], 1_000, 42).unwrap();
    });

    bench("MC 10k iterations", 100, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &[], 10_000, 42).unwrap();
    });

    bench("MC 100k iterations", 10, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &[], 100_000, 42).unwrap();
    });

    bench("MC 1M iterations", 1, || {
        let _ = equity_mc_vs_hand_checked(&aces, &kings, &[], 1_000_000, 42).unwrap();
    });

    println!("\n{:-<78}", "");
    println!("\n=== Recommendations ===\n");
    println!("For known hands:");
    println!("  • River/Turn: Always use EXACT (instant)");
    println!("  • Flop: Use EXACT if < 4 players (< 100ms)");
    println!("  • Preflop: Use EXACT if heads-up (< 100ms), else MC with 10k-100k iterations");
    println!("\nFor random opponents:");
    println!("  • Always use Monte Carlo");
    println!("  • 10k iterations: Good for real-time (< 100ms)");
    println!("  • 100k iterations: High accuracy (< 1s)");
    println!("  • 1M iterations: Research-grade accuracy");
    println!("\nNote: Exact equity preflop with 3+ players can take 100ms-1s");
    println!("      (still very fast, but use MC for real-time applications)");
    println!();
}
