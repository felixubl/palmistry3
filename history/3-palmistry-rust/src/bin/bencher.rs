use palmistry_rust::*;
use rand::rngs::StdRng;
use rand::{SeedableRng};

fn gen_random_hand<R: rand::Rng>(rng: &mut R) -> Hand {
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
    let n: usize = std::env::var("N").ok().and_then(|s| s.parse().ok()).unwrap_or(5_000_000);
    let mut rng = StdRng::seed_from_u64(42);
    let hands: Vec<Hand> = (0..n).map(|_| gen_random_hand(&mut rng)).collect();

    let mut acc: i64 = 0;
    for h in &hands {
        let s = evaluate_u32(h);
        let (cat, r0, r1, r2, r3, r4) = unpack_score(s);
        acc ^= (cat as i64) ^ r0 as i64 ^ r1 as i64 ^ r2 as i64 ^ r3 as i64 ^ r4 as i64;
    }
    std::hint::black_box(acc);
}
