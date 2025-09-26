use std::time::Instant;
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use palmistry_rust::*;

fn gen_random_hand<R: Rng>(rng: &mut R) -> Hand {
    let mut hand = empty_hand();
    let mut seen = [[false; 13]; 4];
    let mut added = 0;
    while added < 7 {
        let suit = rng.gen_range(0..4) as i32;
        let rank = rng.gen_range(0..13) as i32;
        if !seen[suit as usize][rank as usize] {
            seen[suit as usize][rank as usize] = true;
            let _ = add_card(&mut hand, card_to_int(suit, rank));
            added += 1;
        }
    }
    hand
}

fn main() {
    // N default 1,000,000; override: N=5000000 cargo run --release --example throughput
    let n: usize = std::env::var("N").ok().and_then(|s| s.parse().ok()).unwrap_or(1_000_000);

    // Pre-generate hands so we measure only evaluation
    let mut rng = StdRng::seed_from_u64(42);
    let hands: Vec<Hand> = (0..n).map(|_| gen_random_hand(&mut rng)).collect();

    // Warmup (touch LUTs too)
    let mut sink = 0u32;
    for h in hands.iter().take(20_000) {
        sink ^= evaluate_u32(h);
    }

    // Measure
    let t0 = Instant::now();
    for h in &hands {
        sink ^= evaluate_u32(h);
    }
    let dt = t0.elapsed();

    // Report
    let secs = dt.as_secs_f64();
    let hps = (n as f64) / secs;
    let ns_per = (dt.as_nanos() as f64) / (n as f64);

    eprintln!("accumulator (ignore): {}", sink); // prevents over-optimization
    println!(
        "LUT evaluator: processed {} hands in {:.3} s  →  {:>12.0} hands/s  (≈{:>7.1} ns/hand)",
        n, secs, hps, ns_per
    );
}
