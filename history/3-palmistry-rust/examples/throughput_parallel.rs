use std::time::Instant;
use rand::{rngs::StdRng, Rng, SeedableRng};
use rayon::prelude::*;
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
    // default 5_000_000; override with: N=20000000 cargo run --release --example throughput_parallel
    let n: usize = std::env::var("N").ok().and_then(|s| s.parse().ok()).unwrap_or(5_000_000);

    // pre-generate hands (serial; cheap) so the timing is just evaluation
    let mut rng = StdRng::seed_from_u64(42);
    let hands: Vec<Hand> = (0..n).map(|_| gen_random_hand(&mut rng)).collect();

    // warmup (touch LUTs and JIT-like caches)
    let _ = hands.par_iter().take(100_000).map(|h| evaluate_u32(h)).count();

    // measure in parallel: XOR is associative, so reduce is fine
    let t0 = Instant::now();
    let sink = hands
        .par_iter()
        .map(|h| evaluate_u32(h) as u64)
        .reduce(|| 0u64, |a, b| a ^ b);
    let dt = t0.elapsed();

    let secs = dt.as_secs_f64();
    let hps = (n as f64) / secs;
    let ns_per = (dt.as_nanos() as f64) / (n as f64);

    eprintln!("accumulator (ignore): {}", sink);
    println!(
        "Parallel LUT evaluator: processed {} hands in {:.3} s  →  {:>12.0} hands/s  (≈{:>7.1} ns/hand)",
        n, secs, hps, ns_per
    );
}
