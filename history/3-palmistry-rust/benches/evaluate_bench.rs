use criterion::{black_box, criterion_group, criterion_main, Criterion, Throughput, BenchmarkId};
use rand::{Rng, SeedableRng};
use rand::rngs::StdRng;

use palmistry_rust::*; // assumes lib.rs re-exports Hand, MASK13, helpers, and evaluator fns

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

fn evaluate_many_bench(c: &mut Criterion) {
    // Pick N from env or default to 1,000,000
    let n: usize = std::env::var("N")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(1_000_000);

    let mut rng = StdRng::seed_from_u64(42);
    let hands: Vec<Hand> = (0..n).map(|_| gen_random_hand(&mut rng)).collect();

    let mut group = c.benchmark_group("evaluate_many");
    group.throughput(Throughput::Elements(n as u64));

    group.bench_function(BenchmarkId::from_parameter(format!("{} hands", n)), |b| {
        b.iter(|| {
            let mut acc: i64 = 0;
            for h in &hands {
                let s = evaluate_u32(black_box(h));
                let (cat, r0, r1, r2, r3, r4) = unpack_score(s);
                acc ^= (cat as i64) ^ r0 as i64 ^ r1 as i64 ^ r2 as i64 ^ r3 as i64 ^ r4 as i64;
            }
            black_box(acc)
        });
    });
    group.finish();

    // Optional microbench: straight detection over random masks
    let masks: Vec<u16> = (0..1_000_000)
        .map(|_| rand::Rng::gen::<u16>(&mut rng) & MASK13)
        .collect();

    c.bench_function("has_run5 1M masks (via straight_end_from_mask)", |b| {
        b.iter(|| {
            let mut s = 0i64;
            for &m in &masks {
                s += (straight_end_from_mask(black_box(m)) >= 0) as i64;
            }
            black_box(s)
        });
    });
}

criterion_group!(benches, evaluate_many_bench);
criterion_main!(benches);
