# Golf Card Game - Web Implementation

This directory contains a web interface for the Golf card game.

## Related Projects

This UI is part of a larger golf application ecosystem. Related components can be found in:
- [Core Golf Service](../../cpp/golf_service) - Backend implementation
- [Golf gRPC Service](../../cpp/golf_grpc) - gRPC interface
- [Original Golf UI](../golf_ui) - Previous version

## Features
- **Dark Mode**: Toggle between light and dark themes for comfortable viewing in any environment

## Game Rules
Golf is a card game for 2-6 players. The objective is to get the lowest score.

### Setup
- Each player gets 4 cards face down in a 2x2 grid
- Players may peek at two of their cards at the start

### Gameplay
- On your turn, draw a card from the deck or discard pile
- You may swap it with one of your cards or discard it
- The goal is to get the lowest total value of cards
- Any player may "knock" to signal the final round

### Scoring
- Number cards are worth their face value
- Face cards (J, Q, K) are worth 10 points
- Aces are worth 1 point
- Matching cards in the same column cancel out (worth 0)

## Future Improvements
- Multiplayer support via WebSockets
- Persistent game state using localStorage
- Animation and sound effects
- Additional game variants
- AI opponents
