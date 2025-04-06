use serde::{Deserialize, Serialize};

enum Suit { Clubs, Diamonds, Hearts, Spades }
enum Rank { Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Jack, Queen, King, Ace, }

#[derive(Serialize, Deserialize, Debug)]
pub struct Card {
    pub suit: Suit,
    pub rank: Rank,
}
