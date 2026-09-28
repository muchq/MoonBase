# rummy

The rules engine for basic Rummy (#245): an immutable `GameState` value
type in the style of [`../castle`](../castle), for the games hub.

The rules the engine plays are the contract, stated on `GameState` in
`game_state.h` and pinned one by one in `game_state_test.cc`: ten cards a
seat at two, seven at three or four; one card turned up to start the
discard pile; a turn is a draw from the stock or the discard, any melds
and lay-offs, then a discard, and the card taken from the discard may not
go straight back unless it is the last in the hand; an empty stock is the discard pile turned over under its
top card; the first seat to empty its hand wins and scores what everyone
else still holds. What makes a meld is `arrangedMeld` in `meld.h`: three
or four of a rank, or three or more of a suit in sequence with the ace low
or high but never both.

```bash
bazel test //domains/games/libs/cards/rummy/...
```

`game_state_serde.{h,cc}` is the versioned JSON the games hub stores a
rummy table in — the engine's full truth, stock and every hand included,
so it is server-side only; redaction stays in the hub.
`game_state_serde_test.cc` pins the schema.
