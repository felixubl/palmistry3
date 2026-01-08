//! Release-mode benchmarking runner with meaningful modes.
//!
//! Goals:
//! - Separate generation vs evaluation (so you know what's slow).
//! - Avoid allocating absurd amounts of RAM (no Vec of 1e9 boards).
//! - Provide end-to-end (gen+eval) throughput (real simulator style).
//!
//! Modes (K = 5/6/7):
//!   genK N [chunk]        : generate random K-card hands (ids only), measure generation rate
//!   bbK  N [chunk]        : build bitboards (not timed in eval section) then measure eval-only (single-thread)
//!   idsK N [chunk]        : generate ids (not timed in eval section) then measure ids->bitboard->eval (single-thread)
//!   e2eK N                : end-to-end: generate ids + build bitboard + eval (single-thread, streaming)
//!
//! Parallel modes (require --features parallel):
//!   bbKpar  N [chunk]     : build bitboards (single-thread) then eval-only in parallel
//!   idsKpar N [chunk]     : generate ids (single-thread) then ids->bitboard->eval in parallel
//!   e2eKpar N             : end-to-end streaming in parallel (each thread generates+evals its share)
//!
//! Examples:
//!   cargo run --release --example perf -- e2e7 10000000
//!   cargo run --release --example perf -- bb7  50000000 2000000
//!   cargo run --release --example perf -- ids7 50000000 2000000
//!
//! Parallel:
//!   cargo run --release --features parallel --example perf -- e2e7par 200000000
//!   RAYON_NUM_THREADS=8 cargo run --release --features parallel --example perf -- e2e7par 200000000
//!
//! Notes:
//! - "chunk" defaults to 2_000_000 for chunked modes.
//! - Chunked modes never allocate more than `chunk` items at once.
//! - End-to-end streaming modes allocate almost nothing.

use std::hint::black_box;
use std::time::Instant;

use poker_eval::{evaluate_u32, BitBoard4x13};

#[cfg(feature = "parallel")]
use rayon::prelude::*;

#[derive(Clone)]
struct XorShift64 {
    state: u64,
}
impl XorShift64 {
    #[inline(always)]
    fn new(seed: u64) -> Self {
        Self { state: seed }
    }
    #[inline(always)]
    fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }
    #[inline(always)]
    fn next_u8(&mut self, m: u8) -> u8 {
        (self.next_u64() % (m as u64)) as u8
    }
}

/// Generate a random K-card hand as ids in [0..52), no duplicates.
/// We return a fixed [u8; 7] and only the first K entries are valid.
#[inline(always)]
fn gen_hand_ids_7(rng: &mut XorShift64, k: usize) -> [u8; 7] {
    let mut used: u64 = 0; // bits 0..51
    let mut out = [0u8; 7];
    let mut i = 0usize;
    while i < k {
        let id = rng.next_u8(52);
        let bit = 1u64 << id;
        if (used & bit) == 0 {
            used |= bit;
            out[i] = id;
            i += 1;
        }
    }
    out
}

#[inline(always)]
fn build_bitboard_from_ids(ids: &[u8; 7], k: usize) -> BitBoard4x13 {
    let mut b = BitBoard4x13::new();
    for &id in ids.iter().take(k) {
        b.add_id(id);
    }
    b
}

fn report_rate(label: &str, n: u64, dt_secs: f64) {
    let n_f = n as f64;
    let ns_op = (dt_secs * 1e9) / n_f;
    let hps = n_f / dt_secs;
    println!("{label:20}: {:6.0} ns/op   ({:>12} iterations)", ns_op, n);
    println!("{label:20}: {:>10.3} ms      {:>12.0} /s", dt_secs * 1e3, hps);
}

#[cfg(feature = "parallel")]
fn report_rate_par(label: &str, n: u64, dt_secs: f64) {
    let threads = rayon::current_num_threads() as f64;
    let n_f = n as f64;
    let wall_ns_op = (dt_secs * 1e9) / n_f;
    let approx_core_ns_op = wall_ns_op * threads; // interpretable "per-core cost" estimate
    let hps = n_f / dt_secs;

    println!(
        "{label:20}: {:6.0} ns/op   ({:>12} iterations)  [threads={}]",
        wall_ns_op,
        n,
        rayon::current_num_threads()
    );
    println!(
        "{label:20}: {:>10.3} ms      {:>12.0} /s  [~{:5.1} ns/op/core]",
        dt_secs * 1e3,
        hps,
        approx_core_ns_op
    );
}

/// Chunk helper: iterate in chunks without overflowing.
#[inline]
fn chunk_loop(mut n: u64, chunk: u64, mut f: impl FnMut(u64)) {
    while n > 0 {
        let c = n.min(chunk);
        f(c);
        n -= c;
    }
}

// --------------------
// MODE: generation only
// --------------------

fn bench_gen_ids(k: usize, n: u64, chunk: u64) {
    let mut rng = XorShift64::new(0x1234_5678_9ABC_DEF0);
    let start = Instant::now();

    let mut acc: u64 = 0;
    chunk_loop(n, chunk, |c| {
        for _ in 0..c {
            let ids = gen_hand_ids_7(&mut rng, k);
            acc = acc.wrapping_add(ids[0] as u64).wrapping_add(ids[k - 1] as u64);
        }
    });

    let dt = start.elapsed().as_secs_f64();
    black_box(acc);
    report_rate(&format!("GenerateIds{}", k), n, dt);
}

// --------------------
// MODE: bbK (eval-only)
// --------------------

fn bench_bb_eval_only_seq(k: usize, n: u64, chunk: u64) {
    let mut rng = XorShift64::new(0x1111_2222_3333_4444);

    for _ in 0..50_000 {
        let ids = gen_hand_ids_7(&mut rng, k);
        let b = build_bitboard_from_ids(&ids, k);
        black_box(evaluate_u32(&b).0);
    }

    let mut eval_acc: u32 = 0;
    let mut eval_time = 0.0f64;
    let mut gen_time = 0.0f64;

    chunk_loop(n, chunk, |c| {
        let t0 = Instant::now();
        let mut boards: Vec<BitBoard4x13> = Vec::with_capacity(c as usize);
        for _ in 0..c {
            let ids = gen_hand_ids_7(&mut rng, k);
            boards.push(build_bitboard_from_ids(&ids, k));
        }
        gen_time += t0.elapsed().as_secs_f64();

        let t1 = Instant::now();
        for b in &boards {
            eval_acc = eval_acc.wrapping_add(evaluate_u32(b).0);
        }
        eval_time += t1.elapsed().as_secs_f64();

        black_box(&boards);
    });

    black_box(eval_acc);

    println!("BuildBitboards{:<2}      : {:>10.3} ms", k, gen_time * 1e3);
    report_rate(&format!("EvalOnlyBB{}", k), n, eval_time);
}

#[cfg(feature = "parallel")]
fn bench_bb_eval_only_par(k: usize, n: u64, chunk: u64) {
    let mut rng = XorShift64::new(0x1111_2222_3333_4444);

    for _ in 0..50_000 {
        let ids = gen_hand_ids_7(&mut rng, k);
        let b = build_bitboard_from_ids(&ids, k);
        black_box(evaluate_u32(&b).0);
    }

    let mut eval_acc: u32 = 0;
    let mut eval_time = 0.0f64;
    let mut gen_time = 0.0f64;

    chunk_loop(n, chunk, |c| {
        let t0 = Instant::now();
        let mut boards: Vec<BitBoard4x13> = Vec::with_capacity(c as usize);
        for _ in 0..c {
            let ids = gen_hand_ids_7(&mut rng, k);
            boards.push(build_bitboard_from_ids(&ids, k));
        }
        gen_time += t0.elapsed().as_secs_f64();

        let t1 = Instant::now();
        let chunk_sum: u32 = boards
            .par_iter()
            .map(|b| evaluate_u32(b).0)
            .reduce(|| 0u32, |a, b| a.wrapping_add(b));
        eval_acc = eval_acc.wrapping_add(chunk_sum);
        eval_time += t1.elapsed().as_secs_f64();

        black_box(&boards);
    });

    black_box(eval_acc);

    println!("BuildBitboards{:<2}      : {:>10.3} ms", k, gen_time * 1e3);
    report_rate_par(&format!("EvalOnlyBB{}Par", k), n, eval_time);
}

// --------------------
// MODE: idsK (ids->bb->eval)
// --------------------

fn bench_ids_to_eval_seq(k: usize, n: u64, chunk: u64) {
    let mut rng = XorShift64::new(0x9999_AAAA_BBBB_CCCC);

    for _ in 0..50_000 {
        let ids = gen_hand_ids_7(&mut rng, k);
        let b = build_bitboard_from_ids(&ids, k);
        black_box(evaluate_u32(&b).0);
    }

    let mut eval_acc: u32 = 0;
    let mut eval_time = 0.0f64;
    let mut gen_time = 0.0f64;

    chunk_loop(n, chunk, |c| {
        let t0 = Instant::now();
        let mut hands: Vec<[u8; 7]> = Vec::with_capacity(c as usize);
        for _ in 0..c {
            hands.push(gen_hand_ids_7(&mut rng, k));
        }
        gen_time += t0.elapsed().as_secs_f64();

        let t1 = Instant::now();
        for ids in &hands {
            let b = build_bitboard_from_ids(ids, k);
            eval_acc = eval_acc.wrapping_add(evaluate_u32(&b).0);
        }
        eval_time += t1.elapsed().as_secs_f64();

        black_box(&hands);
    });

    black_box(eval_acc);

    println!("GenerateIds{:<2}         : {:>10.3} ms", k, gen_time * 1e3);
    report_rate(&format!("IdsToEval{}", k), n, eval_time);
}

#[cfg(feature = "parallel")]
fn bench_ids_to_eval_par(k: usize, n: u64, chunk: u64) {
    let mut rng = XorShift64::new(0x9999_AAAA_BBBB_CCCC);

    for _ in 0..50_000 {
        let ids = gen_hand_ids_7(&mut rng, k);
        let b = build_bitboard_from_ids(&ids, k);
        black_box(evaluate_u32(&b).0);
    }

    let mut eval_acc: u32 = 0;
    let mut eval_time = 0.0f64;
    let mut gen_time = 0.0f64;

    chunk_loop(n, chunk, |c| {
        let t0 = Instant::now();
        let mut hands: Vec<[u8; 7]> = Vec::with_capacity(c as usize);
        for _ in 0..c {
            hands.push(gen_hand_ids_7(&mut rng, k));
        }
        gen_time += t0.elapsed().as_secs_f64();

        let t1 = Instant::now();
        let chunk_sum: u32 = hands
            .par_iter()
            .map(|ids| {
                let b = build_bitboard_from_ids(ids, k);
                evaluate_u32(&b).0
            })
            .reduce(|| 0u32, |a, b| a.wrapping_add(b));
        eval_acc = eval_acc.wrapping_add(chunk_sum);
        eval_time += t1.elapsed().as_secs_f64();

        black_box(&hands);
    });

    black_box(eval_acc);

    println!("GenerateIds{:<2}         : {:>10.3} ms", k, gen_time * 1e3);
    report_rate_par(&format!("IdsToEval{}Par", k), n, eval_time);
}

// --------------------
// MODE: e2eK (streaming gen+eval)
// --------------------

fn bench_e2e_seq(k: usize, n: u64) {
    let mut rng = XorShift64::new(0xDEAD_BEEF_F00D_CAFE);

    for _ in 0..50_000 {
        let ids = gen_hand_ids_7(&mut rng, k);
        let b = build_bitboard_from_ids(&ids, k);
        black_box(evaluate_u32(&b).0);
    }

    let start = Instant::now();
    let mut acc: u32 = 0;

    for _ in 0..n {
        let ids = gen_hand_ids_7(&mut rng, k);
        let b = build_bitboard_from_ids(&ids, k);
        acc = acc.wrapping_add(evaluate_u32(&b).0);
    }

    let dt = start.elapsed().as_secs_f64();
    black_box(acc);
    report_rate(&format!("EndToEnd{}", k), n, dt);
}

#[cfg(feature = "parallel")]
fn bench_e2e_par(k: usize, n: u64) {
    {
        let mut rng = XorShift64::new(0xDEAD_BEEF_F00D_CAFE);
        for _ in 0..50_000 {
            let ids = gen_hand_ids_7(&mut rng, k);
            let b = build_bitboard_from_ids(&ids, k);
            black_box(evaluate_u32(&b).0);
        }
    }

    let start = Instant::now();

    let acc: u32 = (0..n)
        .into_par_iter()
        .fold(
            || {
                let tid = rayon::current_thread_index().unwrap_or(0) as u64;
                let seed = 0xDEAD_BEEF_F00D_CAFE_u64
                    ^ tid.wrapping_mul(0x9E37_79B9_7F4A_7C15);
                (XorShift64::new(seed), 0u32)
            },
            |(mut rng, mut local_acc), _| {
                let ids = gen_hand_ids_7(&mut rng, k);
                let b = build_bitboard_from_ids(&ids, k);
                local_acc = local_acc.wrapping_add(evaluate_u32(&b).0);
                (rng, local_acc)
            },
        )
        .map(|(_, a)| a)
        .reduce(|| 0u32, |a, b| a.wrapping_add(b));

    let dt = start.elapsed().as_secs_f64();
    black_box(acc);
    report_rate_par(&format!("EndToEnd{}Par", k), n, dt);
}

// --------------------
// CLI parsing
// --------------------

fn usage() -> ! {
    eprintln!("usage: perf MODE N [chunk]");
    eprintln!();
    eprintln!("MODE examples:");
    eprintln!("  gen7    50000000 2000000");
    eprintln!("  bb7     50000000 2000000");
    eprintln!("  ids7    50000000 2000000");
    eprintln!("  e2e7    50000000");
    eprintln!("  bb7par  50000000 2000000   (needs --features parallel)");
    eprintln!("  ids7par 50000000 2000000   (needs --features parallel)");
    eprintln!("  e2e7par 200000000           (needs --features parallel)");
    std::process::exit(2);
}

/// Extract K from mode names like: gen7, bb6, ids5par, e2e7par.
///
/// Old bug: looking only at the *last* char fails for "...par".
fn parse_k(mode: &str) -> Option<usize> {
    let base = mode.strip_suffix("par").unwrap_or(mode);
    // find the last digit 5/6/7 scanning from the end
    for b in base.bytes().rev() {
        match b {
            b'5' => return Some(5),
            b'6' => return Some(6),
            b'7' => return Some(7),
            _ => {}
        }
    }
    None
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        usage();
    }

    let mode = args[1].as_str();
    let n: u64 = args[2].parse().ok().unwrap_or_else(|| usage());
    let k = parse_k(mode).unwrap_or_else(|| usage());

    let chunk: u64 = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(2_000_000);

    match mode {
        m if m.starts_with("gen") => bench_gen_ids(k, n, chunk),
        m if m.starts_with("bb") && !m.contains("par") => bench_bb_eval_only_seq(k, n, chunk),
        m if m.starts_with("ids") && !m.contains("par") => bench_ids_to_eval_seq(k, n, chunk),
        m if m.starts_with("e2e") && !m.contains("par") => bench_e2e_seq(k, n),

        #[cfg(feature = "parallel")]
        m if m.starts_with("bb") && m.contains("par") => bench_bb_eval_only_par(k, n, chunk),
        #[cfg(feature = "parallel")]
        m if m.starts_with("ids") && m.contains("par") => bench_ids_to_eval_par(k, n, chunk),
        #[cfg(feature = "parallel")]
        m if m.starts_with("e2e") && m.contains("par") => bench_e2e_par(k, n),

        #[cfg(not(feature = "parallel"))]
        m if m.contains("par") => {
            eprintln!("Parallel mode requires: --features parallel");
            std::process::exit(2);
        }

        _ => usage(),
    }
}
