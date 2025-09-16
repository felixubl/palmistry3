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
        [1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ]
).T


@njit
def get_hand_vector(cards):
    n_rows, n_cols = cards.shape
    out = np.empty(n_rows, np.int32)
    for i in range(n_rows):
        s = 0
        for j in range(n_cols):
            s += cards[i, j]
        out[i] = s
    return out

@njit
def find_flush(hand_vector):

    for i in range(0, 4):
        if hand_vector[i] > 4:
            return True, i
    
    return False, None

def get_flush_cards(cards, index):
    flush_row = cards[index, :]

    dora = np.diag(flush_row)

    emil = dora.any(axis=0)

    flush_cards = cards * emil

    return flush_cards


@njit
def find_high_card(hand_vector):
    for i in range(len(hand_vector) - 1, -1, -1):
        if hand_vector[i] != 0:
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
