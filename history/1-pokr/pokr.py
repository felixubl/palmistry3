import numpy as np
from numba import njit


quad_2s = np.array(
    [
        [1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ]
).T

straight_flush = np.array(
    [
        [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
        [1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ]
).T


@njit(cache=True, fastmath=True)
def get_hand_vector(cards):
    n_rows, n_cols = cards.shape
    out = np.empty(n_rows, np.int32)
    for i in range(n_rows):
        s = 0
        for j in range(n_cols):
            s += cards[i, j]
        out[i] = s
    return out


def find_straight_flush(cards):
    is_straight_flush = 0
    hand_vector = get_hand_vector(cards)
    is_flush, suit_idx = find_flush(hand_vector)

    if is_flush:
        flush_cards = get_flush_cards(cards, suit_idx)

        flush_cards_hand_vector = get_hand_vector(flush_cards)

        is_straight_flush = find_straight(flush_cards_hand_vector)

    return is_straight_flush


@njit(cache=True, fastmath=True)
def find_quads(hand_vector):
    for i in range(16, -1, -1):
        if hand_vector[i] == 4:
            return i
    return -1


@njit(cache=True, fastmath=True)
def find_full_house(hand_vector):
    i3 = -1
    i2 = -1
    for i in range(16, -1, -1):
        v = hand_vector[i]
        if i3 == -1 and v == 3:
            i3 = i
            if i2 != -1:
                break
        elif i2 == -1 and v == 2:
            i2 = i
            if i3 != -1:
                break
    if i3 == -1 or i2 == -1:
        return -1
    return i3 * 100 + i2


@njit(cache=True, fastmath=True)
def find_flush(hand_vector):
    for i in range(0, 4):
        if hand_vector[i] > 4:
            return True, i

    return False, None


@njit(cache=True, fastmath=True)
def get_flush_cards(cards, index):
    m, n = cards.shape
    # Count kept columns
    cnt = 0
    for j in range(n):
        if cards[index, j] != 0:
            cnt += 1
    # Allocate and copy
    out = np.empty((m, cnt), dtype=cards.dtype)
    pos = 0
    for j in range(n):
        if cards[index, j] != 0:
            for i in range(m):
                out[i, pos] = cards[i, j]
            pos += 1
    return out


@njit(cache=True, fastmath=True)
def find_straight(hand_vector):
    n = hand_vector.size
    if n != 17:
        raise TypeError("Not a Hand Vector")
    run_len = 0
    for i in range(n - 1, 3, -1):
        if hand_vector[i] != 0:
            run_len += 1
        else:
            if run_len >= 5:
                return i + run_len
            run_len = 0
    if run_len >= 5:
        return 4 + run_len - 1
    if (
        (hand_vector[16] != 0)
        and (hand_vector[4] != 0)
        and (hand_vector[5] != 0)
        and (hand_vector[6] != 0)
        and (hand_vector[7] != 0)
    ):
        return 7
    return 0


@njit(cache=True, fastmath=True)
def find_trips(hand_vector):
    for i in range(16, -1, -1):
        if hand_vector[i] == 3:
            return i
    return -1


@njit(cache=True, fastmath=True)
def find_pairs(hand_vector):
    for i in range(16, -1, -1):
        if hand_vector[i] == 2:
            return i
    return -1


@njit(cache=True, fastmath=True)
def find_high_card(hand_vector):
    for i in range(len(hand_vector) - 1, -1, -1):
        if hand_vector[i] == 1:
            return i

    return -1


if __name__ == "__main__":
    hand_1 = quad_2s
    hand_2 = straight_flush

    hand_1_hand_vector = get_hand_vector(hand_1)

    print(hand_1_hand_vector)

    hand_1_flush, hand_1_flush_mask = find_flush(hand_1_hand_vector)

    print(find_high_card(hand_1_hand_vector))

    # hand_1_flush_cards = get_flush_cards(hand_1, hand_1_flush_mask)

    # print(hand_1_flush_cards)

    hand_2_hand_vector = get_hand_vector(hand_2)

    print(hand_2_hand_vector)

    hand_2_flush, hand_2_flush_mask = find_flush(hand_2_hand_vector)

    hand_2_flush_cards = get_flush_cards(hand_2, hand_2_flush_mask)

    print(hand_2_flush_cards)

    hand_2_flush_cards_hand_vector = get_hand_vector(hand_2_flush_cards)

    print(hand_2_flush_cards_hand_vector, hand_2_flush_cards_hand_vector.shape)

    print(find_high_card(hand_2_flush_cards_hand_vector))

    print(find_straight(hand_2_flush_cards_hand_vector))

    print(find_straight_flush(hand_2))

    print(find_straight_flush(hand_1))

    print(find_quads(hand_1_hand_vector))
