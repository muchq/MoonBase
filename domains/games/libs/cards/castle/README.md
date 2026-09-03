# castle

The rules engine for Castle (the shedding game also played as Palace):
immutable `Player` and `GameState` value types in the style of
[`../golf`](../golf), for the games hub.

The rules the engine plays are the contract, stated on `GameState` in
`game_state.h` and pinned one by one in `game_state_test.cc`: three
face-down, three face-up, three in hand; a setup phase of hand/face-up
swaps; lowest ordinary card opens; equal-or-higher plays, twos reset,
tens and four of a kind burn; must play if able, otherwise pick up the
pile; face-up then face-down rows once the hand is gone; first out wins,
last holder loses.

```bash
bazel test //domains/games/libs/cards/castle/...
```
