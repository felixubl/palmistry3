//! Batch evaluation helpers.
//!
//! Goal: keep evaluation separate from generation.
//!
//! - Generation creates Vec<BitBoard4x13> (or streams them).
//! - Evaluation consumes &[BitBoard4x13] and produces scores or reductions.
//!
//! This file provides a very cheap "sum of packed scores" reduction, which is
//! great for benchmarks because it prevents the compiler from optimizing away
//! work but avoids allocating an output array.

use crate::{evaluate_u32, BitBoard4x13};

/// Sequential: evaluate all boards and return a wrapping sum of packed u32 scores.
///
/// Why sum?
/// - It forces all evaluations to happen (can't be optimized away easily).
/// - It's cheaper than allocating a Vec<Score> for benchmarks.
#[inline]
pub fn eval_sum_u32(boards: &[BitBoard4x13]) -> u32 {
    let mut acc: u32 = 0;
    for b in boards {
        acc = acc.wrapping_add(evaluate_u32(b).0);
    }
    acc
}

/// Sequential: evaluate + write each packed score into `out`, and return a wrapping sum.
///
/// Use this when you want to benchmark "evaluate + write output buffer".
#[inline]
pub fn eval_sum_u32_in_place(boards: &[BitBoard4x13], out: &mut [u32]) -> u32 {
    assert_eq!(boards.len(), out.len());
    let mut acc: u32 = 0;
    for (i, b) in boards.iter().enumerate() {
        let v = evaluate_u32(b).0;
        out[i] = v;
        acc = acc.wrapping_add(v);
    }
    acc
}

#[cfg(feature = "parallel")]
mod par {
    use super::*;
    use rayon::prelude::*;

    /// Parallel: evaluate all boards and return a wrapping sum of packed u32 scores.
    ///
    /// Best when `boards` is large (millions). For small N, overhead dominates.
    pub fn eval_sum_u32_par(boards: &[BitBoard4x13]) -> u32 {
        boards
            .par_iter()
            .map(|b| evaluate_u32(b).0)
            .reduce(|| 0u32, |a, b| a.wrapping_add(b))
    }

    pub fn eval_sum_u32_in_place_par(boards: &[BitBoard4x13], out: &mut [u32]) -> u32 {
        assert_eq!(boards.len(), out.len());

        // Parallel fill
        out.par_iter_mut()
            .zip(boards.par_iter())
            .for_each(|(dst, b)| {
                *dst = evaluate_u32(b).0;
            });

        // Parallel reduction over out
        out.par_iter()
            .copied()
            .reduce(|| 0u32, |a, b| a.wrapping_add(b))
    }
}

#[cfg(feature = "parallel")]
pub use par::{eval_sum_u32_in_place_par, eval_sum_u32_par};
