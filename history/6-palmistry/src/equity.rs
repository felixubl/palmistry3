//! Equity calculation: Monte Carlo simulation and exact enumeration.

use crate::{evaluate_u32, BitBoard4x13};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Outcome {
    HeroWin,
    Tie,
    VillainWin,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Default)]
pub struct EquityCounts {
    pub win: u64,
    pub tie: u64,
    pub lose: u64,
}

impl EquityCounts {
    #[inline(always)]
    pub fn total(&self) -> u64 {
        self.win + self.tie + self.lose
    }

    /// "Equity" as win + 0.5*tie, normalized to [0,1].
    pub fn equity(&self) -> f64 {
        let t = self.total() as f64;
        if t == 0.0 {
            return 0.0;
        }
        (self.win as f64 + 0.5 * self.tie as f64) / t
    }

    pub fn probs(&self) -> (f64, f64, f64) {
        let t = self.total() as f64;
        if t == 0.0 {
            return (0.0, 0.0, 0.0);
        }
        (
            self.win as f64 / t,
            self.tie as f64 / t,
            self.lose as f64 / t,
        )
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum EquityError {
    TooManyBoardCards(usize),
    DuplicateCard(u8),
    CardOutOfRange(u8),
    TooFewPlayers,
    TooManyPlayers,
}

#[inline(always)]
fn card_bit(id: u8) -> Result<u64, EquityError> {
    if id >= 52 {
        return Err(EquityError::CardOutOfRange(id));
    }
    Ok(1u64 << id)
}

#[inline(always)]
fn add_used(used: &mut u64, id: u8) -> Result<(), EquityError> {
    let bit = card_bit(id)?;
    if (*used & bit) != 0 {
        return Err(EquityError::DuplicateCard(id));
    }
    *used |= bit;
    Ok(())
}

#[inline]
fn validate_inputs(
    hero: &[u8; 2],
    villain: Option<&[u8; 2]>,
    board: &[u8],
) -> Result<u64, EquityError> {
    if board.len() > 5 {
        return Err(EquityError::TooManyBoardCards(board.len()));
    }
    let mut used: u64 = 0;
    add_used(&mut used, hero[0])?;
    add_used(&mut used, hero[1])?;
    if let Some(v) = villain {
        add_used(&mut used, v[0])?;
        add_used(&mut used, v[1])?;
    }
    for &c in board {
        add_used(&mut used, c)?;
    }
    Ok(used)
}

#[inline(always)]
fn fill_remaining_cards(used: u64, out: &mut [u8; 52]) -> usize {
    let mut n = 0usize;
    for id in 0u8..52u8 {
        if (used & (1u64 << id)) == 0 {
            out[n] = id;
            n += 1;
        }
    }
    n
}

#[inline(always)]
fn eval_two_players_unchecked(hero: &[u8; 2], villain: &[u8; 2], board5: &[u8; 5]) -> Outcome {
    // Build board once, then add holes on copies.
    let mut bb_board = BitBoard4x13::new();
    bb_board.add_id(board5[0]);
    bb_board.add_id(board5[1]);
    bb_board.add_id(board5[2]);
    bb_board.add_id(board5[3]);
    bb_board.add_id(board5[4]);

    let mut h = bb_board;
    h.add_id(hero[0]);
    h.add_id(hero[1]);

    let mut v = bb_board;
    v.add_id(villain[0]);
    v.add_id(villain[1]);

    let hs = evaluate_u32(&h).0;
    let vs = evaluate_u32(&v).0;
    if hs > vs {
        Outcome::HeroWin
    } else if hs < vs {
        Outcome::VillainWin
    } else {
        Outcome::Tie
    }
}

/// Compare showdown given full board (5 cards). Skips validation for speed.
pub fn compare_showdown_unchecked(hero: &[u8; 2], villain: &[u8; 2], board: &[u8; 5]) -> Outcome {
    eval_two_players_unchecked(hero, villain, board)
}

/// Compare showdown with validation (no duplicates, ids in range).
pub fn compare_showdown_checked(
    hero: &[u8; 2],
    villain: &[u8; 2],
    board: &[u8; 5],
) -> Result<Outcome, EquityError> {
    let _ = validate_inputs(hero, Some(villain), board)?;
    Ok(eval_two_players_unchecked(hero, villain, board))
}

// -------------------------
// Monte Carlo utilities
// -------------------------

/// xorshift64 for speed (fine for simulation; not crypto).
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
}

/// Fast sampler for card ids 0..51 without division.
/// Pulls 6-bit chunks from cached u64 and rejects >= 52.
#[derive(Clone)]
struct CardSampler52 {
    rng: XorShift64,
    pool: u64,
    bits_left: u8,
}
impl CardSampler52 {
    #[inline(always)]
    fn new(seed: u64) -> Self {
        let mut rng = XorShift64::new(seed);
        let pool = rng.next_u64();
        Self {
            rng,
            pool,
            bits_left: 64,
        }
    }
    #[inline(always)]
    fn next_u6(&mut self) -> u8 {
        if self.bits_left < 6 {
            self.pool = self.rng.next_u64();
            self.bits_left = 64;
        }
        let v = (self.pool & 63) as u8;
        self.pool >>= 6;
        self.bits_left -= 6;
        v
    }
    #[inline(always)]
    fn next_card_id(&mut self) -> u8 {
        loop {
            let v = self.next_u6();
            if v < 52 {
                return v;
            }
        }
    }
}

#[inline(always)]
fn sample_distinct_cards(
    s: &mut CardSampler52,
    used: &mut u64,
    out: &mut [u8],
) -> Result<(), EquityError> {
    for slot in out.iter_mut() {
        loop {
            let id = s.next_card_id();
            let bit = 1u64 << id;
            if (*used & bit) == 0 {
                *used |= bit;
                *slot = id;
                break;
            }
        }
    }
    Ok(())
}

/// Monte Carlo equity vs a *known* villain hand.
/// - `board` length: 0..5
/// - samples remaining community cards
pub fn equity_mc_vs_hand_checked(
    hero: &[u8; 2],
    villain: &[u8; 2],
    board: &[u8],
    iters: u64,
    seed: u64,
) -> Result<EquityCounts, EquityError> {
    let used0 = validate_inputs(hero, Some(villain), board)?;
    let missing = 5usize.saturating_sub(board.len());
    let mut counts = EquityCounts::default();
    let mut s = CardSampler52::new(seed);

    let mut board5 = [0u8; 5];
    for (i, &c) in board.iter().enumerate() {
        board5[i] = c;
    }

    let mut fill = [0u8; 5]; // we'll use only first `missing`
    for _ in 0..iters {
        let mut used = used0;
        sample_distinct_cards(&mut s, &mut used, &mut fill[..missing])?;

        for i in 0..missing {
            board5[board.len() + i] = fill[i];
        }

        match eval_two_players_unchecked(hero, villain, &board5) {
            Outcome::HeroWin => counts.win += 1,
            Outcome::Tie => counts.tie += 1,
            Outcome::VillainWin => counts.lose += 1,
        }
    }
    Ok(counts)
}

/// Monte Carlo equity vs a *random* villain hand (uniform over remaining combos).
/// - `board` length: 0..5
/// - samples villain hole cards + remaining community cards
pub fn equity_mc_vs_random_checked(
    hero: &[u8; 2],
    board: &[u8],
    iters: u64,
    seed: u64,
) -> Result<EquityCounts, EquityError> {
    let used0 = validate_inputs(hero, None, board)?;
    let missing = 5usize.saturating_sub(board.len());
    let mut counts = EquityCounts::default();
    let mut s = CardSampler52::new(seed);

    let mut board5 = [0u8; 5];
    for (i, &c) in board.iter().enumerate() {
        board5[i] = c;
    }

    let mut villain = [0u8; 2];
    let mut fill = [0u8; 5]; // only first missing used

    for _ in 0..iters {
        let mut used = used0;

        // sample villain first, then board runout
        sample_distinct_cards(&mut s, &mut used, &mut villain)?;
        sample_distinct_cards(&mut s, &mut used, &mut fill[..missing])?;

        for i in 0..missing {
            board5[board.len() + i] = fill[i];
        }

        match eval_two_players_unchecked(hero, &villain, &board5) {
            Outcome::HeroWin => counts.win += 1,
            Outcome::Tie => counts.tie += 1,
            Outcome::VillainWin => counts.lose += 1,
        }
    }

    Ok(counts)
}

// -------------------------
// Exact enumeration utilities
// -------------------------

#[inline(always)]
fn bump_counts(counts: &mut EquityCounts, out: Outcome) {
    match out {
        Outcome::HeroWin => counts.win += 1,
        Outcome::Tie => counts.tie += 1,
        Outcome::VillainWin => counts.lose += 1,
    }
}

/// Enumerate all combinations of `missing` cards from `rem[0..m]` and call `f(board5)`.
/// Implemented as specialized nested loops for speed (missing <= 5).
#[inline]
fn enumerate_board_completions(
    rem: &[u8],
    known_board: &[u8],
    missing: usize,
    mut f: impl FnMut([u8; 5]),
) {
    let mut board5 = [0u8; 5];
    for (i, &c) in known_board.iter().enumerate() {
        board5[i] = c;
    }
    let base = known_board.len();

    let m = rem.len();
    match missing {
        0 => {
            f(board5);
        }
        1 => {
            for i in 0..m {
                board5[base] = rem[i];
                f(board5);
            }
        }
        2 => {
            for i in 0..m.saturating_sub(1) {
                let a = rem[i];
                for j in (i + 1)..m {
                    board5[base] = a;
                    board5[base + 1] = rem[j];
                    f(board5);
                }
            }
        }
        3 => {
            for i in 0..m.saturating_sub(2) {
                let a = rem[i];
                for j in (i + 1)..m.saturating_sub(1) {
                    let b = rem[j];
                    for k in (j + 1)..m {
                        board5[base] = a;
                        board5[base + 1] = b;
                        board5[base + 2] = rem[k];
                        f(board5);
                    }
                }
            }
        }
        4 => {
            for i in 0..m.saturating_sub(3) {
                let a = rem[i];
                for j in (i + 1)..m.saturating_sub(2) {
                    let b = rem[j];
                    for k in (j + 1)..m.saturating_sub(1) {
                        let c = rem[k];
                        for l in (k + 1)..m {
                            board5[base] = a;
                            board5[base + 1] = b;
                            board5[base + 2] = c;
                            board5[base + 3] = rem[l];
                            f(board5);
                        }
                    }
                }
            }
        }
        5 => {
            for i in 0..m.saturating_sub(4) {
                let a = rem[i];
                for j in (i + 1)..m.saturating_sub(3) {
                    let b = rem[j];
                    for k in (j + 1)..m.saturating_sub(2) {
                        let c = rem[k];
                        for l in (k + 1)..m.saturating_sub(1) {
                            let d = rem[l];
                            for p in (l + 1)..m {
                                board5[base] = a;
                                board5[base + 1] = b;
                                board5[base + 2] = c;
                                board5[base + 3] = d;
                                board5[base + 4] = rem[p];
                                f(board5);
                            }
                        }
                    }
                }
            }
        }
        _ => {
            // unreachable for valid poker boards
        }
    }
}

/// Exact equity vs a *known* villain hand by enumerating all remaining board runouts.
pub fn equity_exact_vs_hand_checked(
    hero: &[u8; 2],
    villain: &[u8; 2],
    board: &[u8],
) -> Result<EquityCounts, EquityError> {
    let used0 = validate_inputs(hero, Some(villain), board)?;
    let missing = 5usize.saturating_sub(board.len());

    let mut buf = [0u8; 52];
    let nrem = fill_remaining_cards(used0, &mut buf);
    let rem = &buf[..nrem];

    let mut counts = EquityCounts::default();
    enumerate_board_completions(rem, board, missing, |board5| {
        let out = eval_two_players_unchecked(hero, villain, &board5);
        bump_counts(&mut counts, out);
    });

    Ok(counts)
}

/// Exact equity vs a *random* villain hand (uniform over remaining villain combos)
/// AND all remaining board runouts.
///
/// Warning: preflop this can be ~2.1 billion evaluations (still feasible with your speed,
/// but it will take seconds to minutes depending on hardware).
pub fn equity_exact_vs_random_checked(
    hero: &[u8; 2],
    board: &[u8],
) -> Result<EquityCounts, EquityError> {
    let used_hero_board = validate_inputs(hero, None, board)?;
    let missing = 5usize.saturating_sub(board.len());

    // Remaining cards after hero+known board
    let mut buf1 = [0u8; 52];
    let n1 = fill_remaining_cards(used_hero_board, &mut buf1);
    let rem1 = &buf1[..n1];

    let mut counts = EquityCounts::default();

    // Enumerate villain combos from rem1
    for i in 0..rem1.len().saturating_sub(1) {
        let v0 = rem1[i];
        for j in (i + 1)..rem1.len() {
            let v1 = rem1[j];
            let villain = [v0, v1];

            // Build remaining cards after also removing villain
            let mut used = used_hero_board;
            used |= 1u64 << v0;
            used |= 1u64 << v1;

            let mut buf2 = [0u8; 52];
            let n2 = fill_remaining_cards(used, &mut buf2);
            let rem2 = &buf2[..n2];

            enumerate_board_completions(rem2, board, missing, |board5| {
                let out = eval_two_players_unchecked(hero, &villain, &board5);
                bump_counts(&mut counts, out);
            });
        }
    }

    Ok(counts)
}

// -------------------------
// Multi-way equity (3+ players)
// -------------------------

/// Multi-way equity result: one EquityCounts per player.
pub type MultiWayResult = Vec<EquityCounts>;

/// Determine winners from a slice of scores. Returns indices of winning player(s).
/// In case of tie, multiple players win.
#[inline]
fn find_winners(scores: &[u32]) -> Vec<usize> {
    if scores.is_empty() {
        return vec![];
    }
    let best = *scores.iter().max().unwrap();
    scores
        .iter()
        .enumerate()
        .filter(|(_, &s)| s == best)
        .map(|(i, _)| i)
        .collect()
}

/// Monte Carlo multi-way equity with all known hands.
/// - `hands` is a slice of 2-9 player hands (each hand is [u8; 2])
/// - `board` length: 0..5
/// - Returns one EquityCounts per player
pub fn equity_mc_multiway_checked(
    hands: &[&[u8; 2]],
    board: &[u8],
    iters: u64,
    seed: u64,
) -> Result<MultiWayResult, EquityError> {
    let n = hands.len();
    if n < 2 {
        return Err(EquityError::TooFewPlayers);
    }
    if n > 9 {
        return Err(EquityError::TooManyPlayers);
    }
    if board.len() > 5 {
        return Err(EquityError::TooManyBoardCards(board.len()));
    }

    // Validate no duplicates
    let mut used: u64 = 0;
    for hand in hands {
        add_used(&mut used, hand[0])?;
        add_used(&mut used, hand[1])?;
    }
    for &c in board {
        add_used(&mut used, c)?;
    }

    let missing = 5usize.saturating_sub(board.len());
    let mut results = vec![EquityCounts::default(); n];
    let mut s = CardSampler52::new(seed);

    let mut board5 = [0u8; 5];
    for (i, &c) in board.iter().enumerate() {
        board5[i] = c;
    }

    let mut fill = [0u8; 5];
    let mut boards = vec![BitBoard4x13::new(); n];
    let mut scores = vec![0u32; n];

    for _ in 0..iters {
        let mut used_iter = used;
        sample_distinct_cards(&mut s, &mut used_iter, &mut fill[..missing])?;

        for i in 0..missing {
            board5[board.len() + i] = fill[i];
        }

        // Build board base
        let mut bb_board = BitBoard4x13::new();
        for &c in &board5 {
            bb_board.add_id(c);
        }

        // Evaluate each player
        for (i, hand) in hands.iter().enumerate() {
            boards[i] = bb_board;
            boards[i].add_id(hand[0]);
            boards[i].add_id(hand[1]);
            scores[i] = evaluate_u32(&boards[i]).0;
        }

        // Find winner(s)
        let winners = find_winners(&scores);
        if winners.len() == 1 {
            // Sole winner
            results[winners[0]].win += 1;
            for i in 0..n {
                if i != winners[0] {
                    results[i].lose += 1;
                }
            }
        } else {
            // Tie among winners
            for &w in &winners {
                results[w].tie += 1;
            }
            for i in 0..n {
                if !winners.contains(&i) {
                    results[i].lose += 1;
                }
            }
        }
    }

    Ok(results)
}

/// Monte Carlo equity for hero vs (N-1) random villains.
/// - `hero` is the known hand
/// - `num_villains` is 1..8 (total players = num_villains + 1)
/// - `board` length: 0..5
/// - Returns hero's EquityCounts only
pub fn equity_mc_vs_random_multiway_checked(
    hero: &[u8; 2],
    num_villains: usize,
    board: &[u8],
    iters: u64,
    seed: u64,
) -> Result<EquityCounts, EquityError> {
    if num_villains < 1 {
        return Err(EquityError::TooFewPlayers);
    }
    if num_villains > 8 {
        return Err(EquityError::TooManyPlayers);
    }
    if board.len() > 5 {
        return Err(EquityError::TooManyBoardCards(board.len()));
    }

    let mut used0: u64 = 0;
    add_used(&mut used0, hero[0])?;
    add_used(&mut used0, hero[1])?;
    for &c in board {
        add_used(&mut used0, c)?;
    }

    let missing = 5usize.saturating_sub(board.len());
    let mut counts = EquityCounts::default();
    let mut s = CardSampler52::new(seed);

    let mut board5 = [0u8; 5];
    for (i, &c) in board.iter().enumerate() {
        board5[i] = c;
    }

    let mut villains = vec![[0u8; 2]; num_villains];
    let mut fill = [0u8; 5];
    let mut boards = vec![BitBoard4x13::new(); num_villains + 1];
    let mut scores = vec![0u32; num_villains + 1];

    for _ in 0..iters {
        let mut used = used0;

        // Sample villain hands
        for v in &mut villains {
            sample_distinct_cards(&mut s, &mut used, v)?;
        }

        // Sample remaining board
        sample_distinct_cards(&mut s, &mut used, &mut fill[..missing])?;

        for i in 0..missing {
            board5[board.len() + i] = fill[i];
        }

        // Build board base
        let mut bb_board = BitBoard4x13::new();
        for &c in &board5 {
            bb_board.add_id(c);
        }

        // Hero (index 0)
        boards[0] = bb_board;
        boards[0].add_id(hero[0]);
        boards[0].add_id(hero[1]);
        scores[0] = evaluate_u32(&boards[0]).0;

        // Villains
        for (i, v) in villains.iter().enumerate() {
            boards[i + 1] = bb_board;
            boards[i + 1].add_id(v[0]);
            boards[i + 1].add_id(v[1]);
            scores[i + 1] = evaluate_u32(&boards[i + 1]).0;
        }

        // Find winner(s)
        let winners = find_winners(&scores);
        if winners.len() == 1 {
            if winners[0] == 0 {
                counts.win += 1;
            } else {
                counts.lose += 1;
            }
        } else {
            // Tie
            if winners.contains(&0) {
                counts.tie += 1;
            } else {
                counts.lose += 1;
            }
        }
    }

    Ok(counts)
}

/// Exact multi-way equity with all known hands by enumerating all board runouts.
/// - `hands` is a slice of 2-9 player hands
/// - `board` length: 0..5
/// - Warning: can be very slow for preflop scenarios with many players
pub fn equity_exact_multiway_checked(
    hands: &[&[u8; 2]],
    board: &[u8],
) -> Result<MultiWayResult, EquityError> {
    let n = hands.len();
    if n < 2 {
        return Err(EquityError::TooFewPlayers);
    }
    if n > 9 {
        return Err(EquityError::TooManyPlayers);
    }
    if board.len() > 5 {
        return Err(EquityError::TooManyBoardCards(board.len()));
    }

    let mut used: u64 = 0;
    for hand in hands {
        add_used(&mut used, hand[0])?;
        add_used(&mut used, hand[1])?;
    }
    for &c in board {
        add_used(&mut used, c)?;
    }

    let missing = 5usize.saturating_sub(board.len());
    let mut buf = [0u8; 52];
    let nrem = fill_remaining_cards(used, &mut buf);
    let rem = &buf[..nrem];

    let mut results = vec![EquityCounts::default(); n];
    let mut boards = vec![BitBoard4x13::new(); n];
    let mut scores = vec![0u32; n];

    enumerate_board_completions(rem, board, missing, |board5| {
        // Build board base
        let mut bb_board = BitBoard4x13::new();
        for &c in &board5 {
            bb_board.add_id(c);
        }

        // Evaluate each player
        for (i, hand) in hands.iter().enumerate() {
            boards[i] = bb_board;
            boards[i].add_id(hand[0]);
            boards[i].add_id(hand[1]);
            scores[i] = evaluate_u32(&boards[i]).0;
        }

        // Find winner(s)
        let winners = find_winners(&scores);
        if winners.len() == 1 {
            results[winners[0]].win += 1;
            for i in 0..n {
                if i != winners[0] {
                    results[i].lose += 1;
                }
            }
        } else {
            for &w in &winners {
                results[w].tie += 1;
            }
            for i in 0..n {
                if !winners.contains(&i) {
                    results[i].lose += 1;
                }
            }
        }
    });

    Ok(results)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn showdown_compare_deterministic() {
        // Hero: As Ah, Villain: Ks Kh, Board: 2c 3d 4h 5s 9c => hero wins with Aces
        let hero = [12, 25];    // (0*13+12)=Ac? Actually id mapping is suit*13+rank.
        let vill = [11, 24];
        let board = [0, 14, 28, 42, 7];
        let out = compare_showdown_checked(&hero, &vill, &board).unwrap();
        assert_eq!(out, Outcome::HeroWin);
    }

    #[test]
    fn exact_equity_complete_board_is_one_trial() {
        let hero = [0, 1];
        let vill = [2, 3];
        let board = [4, 5, 6, 7, 8];
        let e = equity_exact_vs_hand_checked(&hero, &vill, &board).unwrap();
        assert_eq!(e.total(), 1);
    }

    #[test]
    fn mc_counts_sum() {
        let hero = [0, 1];
        let vill = [2, 3];
        let board: [u8; 3] = [4, 5, 6];
        let e = equity_mc_vs_hand_checked(&hero, &vill, &board, 10000, 123).unwrap();
        assert_eq!(e.total(), 10000);
    }

    #[test]
    fn multiway_complete_board_deterministic() {
        // Three players with complete board - deterministic outcome
        let h1 = [0, 1];   // 2c 3c
        let h2 = [13, 14]; // 2d 3d
        let h3 = [26, 27]; // 2h 3h
        let board = [51, 50, 49, 48, 47]; // Full board

        let results = equity_exact_multiway_checked(&[&h1, &h2, &h3], &board).unwrap();

        // Should be exactly 1 trial
        assert_eq!(results[0].total(), 1);
        assert_eq!(results[1].total(), 1);
        assert_eq!(results[2].total(), 1);

        // All three have same hand (pair of twos), should tie
        assert_eq!(results[0].tie, 1);
        assert_eq!(results[1].tie, 1);
        assert_eq!(results[2].tie, 1);
    }

    #[test]
    fn multiway_mc_counts_correct() {
        let h1 = [0, 1];
        let h2 = [2, 3];
        let h3 = [4, 5];
        let board: [u8; 3] = [6, 7, 8];

        let results = equity_mc_multiway_checked(&[&h1, &h2, &h3], &board, 10000, 456).unwrap();

        // Each player should have exactly 10000 total outcomes
        assert_eq!(results[0].total(), 10000);
        assert_eq!(results[1].total(), 10000);
        assert_eq!(results[2].total(), 10000);

        // Verify all outcomes are accounted for
        // Each iteration produces win/tie/lose for each player
        let total_wins = results.iter().map(|r| r.win).sum::<u64>();
        let total_ties = results.iter().map(|r| r.tie).sum::<u64>();
        let total_loses = results.iter().map(|r| r.lose).sum::<u64>();

        // Total outcomes across all players should be iterations * num_players
        assert_eq!(total_wins + total_ties + total_loses, 10000 * 3);
    }

    #[test]
    fn multiway_vs_random_counts_correct() {
        let hero = [0, 1];
        let board: [u8; 3] = [2, 3, 4];

        let counts = equity_mc_vs_random_multiway_checked(&hero, 2, &board, 5000, 789).unwrap();

        assert_eq!(counts.total(), 5000);
    }

    #[test]
    fn multiway_exact_vs_mc_similar() {
        // With a turn board (4 cards), exact should be tractable
        let h1 = [0, 1];
        let h2 = [13, 14];
        let board = [26, 27, 28, 29]; // 4 cards (turn)

        let exact = equity_exact_multiway_checked(&[&h1, &h2], &board).unwrap();
        let mc = equity_mc_multiway_checked(&[&h1, &h2], &board, 50000, 111).unwrap();

        // Exact totals: 52 - 4 (hands) - 4 (board) = 44 remaining cards
        assert_eq!(exact[0].total(), 44);
        assert_eq!(exact[1].total(), 44);

        // MC totals should be 50000
        assert_eq!(mc[0].total(), 50000);
        assert_eq!(mc[1].total(), 50000);

        // Equity should be similar (within 5%)
        let eq_exact = exact[0].equity();
        let eq_mc = mc[0].equity();
        let diff = (eq_exact - eq_mc).abs();
        assert!(diff < 0.05, "Exact: {}, MC: {}, diff: {}", eq_exact, eq_mc, diff);
    }

    #[test]
    fn multiway_errors() {
        let h1 = [0, 1];

        // Too few players
        let result = equity_mc_multiway_checked(&[&h1], &[], 100, 0);
        assert_eq!(result, Err(EquityError::TooFewPlayers));

        // Too many villains for vs_random
        let result = equity_mc_vs_random_multiway_checked(&h1, 9, &[], 100, 0);
        assert_eq!(result, Err(EquityError::TooManyPlayers));

        // Too many board cards
        let result = equity_mc_vs_random_multiway_checked(&h1, 2, &[0, 1, 2, 3, 4, 5], 100, 0);
        assert!(matches!(result, Err(EquityError::TooManyBoardCards(6))));
    }
}
