#[inline]
pub fn card_to_int(suit: i32, rank: i32) -> i32 {
    (suit << 4) | rank
}

#[inline]
pub fn decode_card_str(card: &str) -> Option<(i32, i32)> {
    if card.len() != 2 {
        return None;
    }
    let rank_char = &card[0..1];
    let suit_char = &card[1..2];

    let rank = match rank_char {
        "A" => 1,
        "2" => 2,
        "3" => 3,
        "4" => 4,
        "5" => 5,
        "6" => 6,
        "7" => 7,
        "8" => 8,
        "9" => 9,
        "T" => 10,
        "J" => 11,
        "Q" => 12,
        "K" => 13,
        _ => return None,
    };

    let suit = match suit_char {
        "c" => 0,
        "d" => 1,
        "h" => 2,
        "s" => 3,
        _ => return None,
    };
    (suit, rank)
}


#[inline]
pub fn decode_card_int(card: i32) -> (i32, i32) {
    let rank = card & 0xF;
    let suit = (card >> 4) & 0x3;
    (suit, rank)
}