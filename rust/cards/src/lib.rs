use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug)]
pub enum Suit {
    Clubs,
    Diamonds,
    Hearts,
    Spades,
}
#[derive(Serialize, Deserialize, Debug)]
pub enum Rank {
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Card {
    pub suit: Suit,
    pub rank: Rank,
}
