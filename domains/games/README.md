# Games Domain

Game engines, services, and libraries.

## Deployed

- [**1d4**](https://1d4.net) — Chess game indexer web UI ([1d4_web](apps/1d4_web)); browse games, enqueue indexing, run ChessQL queries.

## APIs

- [**Golf Service**](apis/golf_service): C++ backend for a golf-themed game.
- [**Golf gRPC**](apis/golf_grpc): gRPC interface for golf game services.
- [**Games WS Backend**](apis/games_ws_backend): WebSocket-based backend for real-time multiplayer games.
- [**Mithril**](apis/mithril): Rust-based game service.
- [**1d4.net**](apis/one_d4): Chess analysis service.
- [**MCPServer**](apis/mcpserver): Multi-protocol game server.

## Apps

- [**FlippyMem**](apps/flippymem): A simple memory game (web-based).
- [**Chess Demo**](apps/chess_demo): Demonstration application for chess-related functionality.
- [**1d4 Web**](apps/1d4_web): Lightweight web UI for the one_d4 chess game indexer — browse indexed games (with username search), enqueue index requests, run ChessQL queries. Vanilla HTML/JS/CSS; deployed at [1d4.net](https://1d4.net).
- [**Wordchains**](apps/wordchains): Interactive solver for Lewis Carroll's "Doublets" (Word Ladder) game. Supports graph generation, shortest path finding, and exploring all paths between words.
- [**Wordchains iOS**](apps/wordchains_ios): SwiftUI iOS app for the Word Chains puzzle game. Quick Play, Daily Challenge, and Time Attack modes. Reuses the same word graph and algorithms as the CLI.

## Libraries

- [**Cards**](libs/cards): C++ library for card game logic and deck management.
- [**Cards Java**](libs/cards_java): Java implementation of card game utilities.
- [**Toyfish**](libs/toyfish): Toy chess engine in Go.
- [**Chess.com Client**](libs/chess_com_client): API client for interacting with Chess.com.
- [**ChessQL**](libs/chessql): Query language/engine for chess data.
- [**Wordchains**](libs/wordchains): Core graph algorithms and data structures for word chain puzzles.

## Protos

- **Golf/Cards Protos**: Protocol buffer definitions for various game services.
