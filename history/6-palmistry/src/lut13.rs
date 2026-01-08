//! LUTs for 13-bit rank masks (0..8191).
//!
//! This mirrors your Python approach: small tables that are extremely cache-friendly.
//!
//! - POPCNT13[mask]      -> number of set bits
//! - HIBIT13[mask]       -> highest set bit index (0..12) or -1
//! - STRAIGHT_END13[mask]-> straight "end rank" index (0..12) or -1
//!                          wheel A2345 returns 3 (Five-high)

use crate::bitboard::MASK13;

const N: usize = 1 << 13;
const WHEEL_MASK: u16 = (1u16 << 12) | (1u16 << 0) | (1u16 << 1) | (1u16 << 2) | (1u16 << 3);

const fn popcount_u16(mut x: u16) -> u8 {
    let mut c: u8 = 0;
    while x != 0 {
        c += (x & 1) as u8;
        x >>= 1;
    }
    c
}

const fn hibit_index_u16(x: u16) -> i8 {
    if x == 0 {
        return -1;
    }
    // scan from bit 12 down to 0 (13-bit domain)
    let mut i: i8 = 12;
    while i >= 0 {
        if (x & (1u16 << (i as u16))) != 0 {
            return i;
        }
        i -= 1;
    }
    -1
}

const fn straight_end_u16(mask: u16) -> i8 {
    // Check 5-bit windows from highest possible start (8) down to 0
    let mut s: i8 = 8;
    while s >= 0 {
        let window: u16 = ((1u16 << 5) - 1) << (s as u16);
        if (mask & window) == window {
            return s + 4; // end index
        }
        s -= 1;
    }

    // Wheel: A 2 3 4 5 (Ace is bit 12, Five is bit 3). Return end=3.
    if (mask & WHEEL_MASK) == WHEEL_MASK {
        return 3;
    }

    -1
}

const fn build_popcnt13() -> [u8; N] {
    let mut arr = [0u8; N];
    let mut i: usize = 0;
    while i < N {
        arr[i] = popcount_u16(i as u16);
        i += 1;
    }
    arr
}

const fn build_hibit13() -> [i8; N] {
    let mut arr = [0i8; N];
    let mut i: usize = 0;
    while i < N {
        arr[i] = hibit_index_u16(i as u16);
        i += 1;
    }
    arr
}

const fn build_straight_end13() -> [i8; N] {
    let mut arr = [0i8; N];
    let mut i: usize = 0;
    while i < N {
        arr[i] = straight_end_u16(i as u16);
        i += 1;
    }
    arr
}

pub const POPCNT13: [u8; N] = build_popcnt13();
pub const HIBIT13: [i8; N] = build_hibit13();
pub const STRAIGHT_END13: [i8; N] = build_straight_end13();

#[inline(always)]
pub fn popcnt13(mask: u16) -> u8 {
    POPCNT13[(mask & MASK13) as usize]
}

#[inline(always)]
pub fn hibit13(mask: u16) -> i8 {
    HIBIT13[(mask & MASK13) as usize]
}

#[inline(always)]
pub fn straight_end13(mask: u16) -> i8 {
    STRAIGHT_END13[(mask & MASK13) as usize]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn popcnt_basic() {
        assert_eq!(popcnt13(0), 0);
        assert_eq!(popcnt13(1), 1);
        assert_eq!(popcnt13((1 << 12) | (1 << 0)), 2);
    }

    #[test]
    fn hibit_basic() {
        assert_eq!(hibit13(0), -1);
        assert_eq!(hibit13(1), 0);
        assert_eq!(hibit13(1 << 12), 12);
    }

    #[test]
    fn straight_and_wheel() {
        // Ten-J-Q-K-A => bits 8..12, end should be 12
        let broadway = (1u16 << 8) | (1u16 << 9) | (1u16 << 10) | (1u16 << 11) | (1u16 << 12);
        assert_eq!(straight_end13(broadway), 12);

        // Wheel A-2-3-4-5 => end should be 3
        assert_eq!(straight_end13(WHEEL_MASK), 3);
    }
}
