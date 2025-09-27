import numpy as np
import random

# import your evaluator functions here
from evaluator import (
    empty_hand, add_card, card_to_int, evaluate, evaluate_u32
)

SUITS = ["♠", "♥", "♦", "♣"]
RANKS = ["2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"]

def deck_index_to_str(deck_index: int) -> str:
    """Convert 0..51 deck index to string like 'A♠'."""
    suit = deck_index // 13
    rank = deck_index % 13
    return RANKS[rank] + SUITS[suit]

def random_hand():
    """Generate a 7-card hand in evaluator format (bitmasks), plus raw deck indices."""
    deck = list(range(52))
    random.shuffle(deck)
    hand = empty_hand()
    cards = deck[:7]
    for c in cards:
        suit = c // 13
        rank = c % 13
        add_card(hand, card_to_int(suit, rank))
    return hand, cards

def main():
    print("Testing 5 random 7-card hands:\n")
    for _ in range(10):
        hand, cards = random_hand()
        score_tuple = evaluate(hand)
        score_u32 = evaluate_u32(hand)

        card_strs = " ".join(deck_index_to_str(c) for c in cards)
        print(f"Hand: {card_strs}")
        print(f" Score tuple: {score_tuple}")
        print(f" Packed u32: {score_u32:#010x}\n")

if __name__ == "__main__":
    main()
