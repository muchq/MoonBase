# castle

The rules engine for Castle (the shedding game also played as Palace):
immutable `Player` and `GameState` value types in the style of
[`../golf`](../golf), for the games hub.

The rules the engine plays are the contract, stated on `GameState` in
`game_state.h` and pinned one by one in `game_state_test.cc`: three
face-down, three face-up, three in hand; a setup phase of hand/face-up
swaps; lowest ordinary card opens; a play matches or beats the count on
top at its rank or higher, or completes the top's four of a kind; twos
reset the pile and tens clear it, four of a kind counts as a ten, and
either way the same seat plays again;
the pile may be picked up on any turn instead of playing; face-up then
face-down rows once the hand is gone; the first seat out wins and ends
the game.

```bash
bazel test //domains/games/libs/cards/castle/...
```

`game_state_serde.{h,cc}` is the versioned JSON the games hub stores a
castle table in — the engine's full truth, draw pile and every hand
included, so it is server-side only; redaction stays in the hub.
`game_state_serde_test.cc` pins the schema.
