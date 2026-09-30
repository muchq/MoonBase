# chess_play

Chess at a games-hub table: the rules engine `games_hub` plays and the row
it stores.

- `game_state.h` — `GameState`, an immutable value: two seats, a variant,
  the start position and the moves from it in UCI, a Fischer clock, and the
  result. Every legal move, and every ending — checkmate, stalemate,
  insufficient material, fifty moves, threefold repetition — comes from
  replaying the moves through `//bazel/3p/chess_library`, which stays behind
  the `.cc` files. Resignation, timeout and abandonment are the players'.
  Time is epoch milliseconds handed in by the caller.
- `RandomKpkOpening` — the `kpk` variant's start: king and pawn against
  king, White to move, White a random seat.
- `game_state_serde.h` — the versioned JSON the games table holds;
  deserializing replays and validates it through `GameState::restore`.

A new variant is a new opening, and a new name `GameState::start` accepts.
