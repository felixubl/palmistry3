import numpy as np
from timeit import timeit
from numba import njit

# --- Data setup --------------------------------------------------------------
# Same mask you had; note .T is a no-op for 1D arrays, so dropped.
SUIT_MASK_17 = np.r_[np.ones(4, dtype=bool), np.zeros(13, dtype=bool)]

# Generate a big batch of random "hand vectors"
# Adjust N to taste; bigger N reduces noise.
N = 200_000
rng = np.random.default_rng(42)
hands = rng.integers(0, 7, size=(N, 17), dtype=np.int64)  # 0..6 counts as example

# --- Implementations (use -1 sentinel instead of None) -----------------------
@njit(cache=True, fastmath=False)
def find_flush_nb(hand_vector):
    # Only first 4 entries (suits); returns (found, index or -1)
    for i in range(4):
        if hand_vector[i] > 4:
            return True, i
    return False, -1

def find_flush_np(hand_vector, suit_mask=SUIT_MASK_17):
    mask = (hand_vector > 4) & suit_mask
    if mask.any():
        return True, int(np.argmax(mask))
    else:
        return False, -1

# --- Warm-up JIT so we don't count compile time ------------------------------
_ = find_flush_nb(hands[0])

# --- Quick correctness check on a sample -------------------------------------
for idx in [0, 1, 2, 3, 10, 1000, 12345]:
    a = find_flush_nb(hands[idx])
    b = find_flush_np(hands[idx])
    assert a == b, f"Mismatch at {idx}: numba={a} numpy={b}"
print("Correctness check passed on samples.")

# --- Batch runners that avoid per-call Python overhead -----------------------
@njit(cache=True)
def run_nb_batch(hands):
    found = 0
    acc = 0
    for i in range(hands.shape[0]):
        f, idx = find_flush_nb(hands[i])
        found += 1 if f else 0
        acc += idx  # consume result so compiler can't elide work
    return found, acc

def run_np_batch(hands):
    found = 0
    acc = 0
    for i in range(hands.shape[0]):
        f, idx = find_flush_np(hands[i])
        found += 1 if f else 0
        acc += idx
    return found, acc

# Warm up the nb batch too
_ = run_nb_batch(hands)

# --- Benchmark ---------------------------------------------------------------
# Adjust number=… to trade accuracy vs speed of the benchmark run itself.
t_nb = timeit(lambda: run_nb_batch(hands), number=5)
t_np = timeit(lambda: run_np_batch(hands), number=5)

print(f"Numba loop: {t_nb:.4f} s (for {N*5:,} hands total)")
print(f"NumPy per-hand: {t_np:.4f} s (for {N*5:,} hands total)")

# Optional: vectorized NumPy baseline for perspective (checks all hands at once)
def run_np_vectorized(hands, suit_mask=SUIT_MASK_17):
    # Finds first suit>4 per hand (or -1 if none). This is a different API (batch),
    # shown just to see the ceiling of pure NumPy when you can vectorize across N.
    mask = (hands[:, :4] > 4)  # only need first 4 columns given the mask
    any_flush = mask.any(axis=1)
    # Argmax returns 0 if no True; we fix those to -1
    idxs = np.where(any_flush, np.argmax(mask, axis=1), -1)
    return any_flush, idxs

t_np_vec = timeit(lambda: run_np_vectorized(hands), number=5)
print(f"NumPy fully vectorized batch: {t_np_vec:.4f} s (for {N*5:,} hands total)")
