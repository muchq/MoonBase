# MoonBase

![Test and Publish](https://github.com/muchq/MoonBase/actions/workflows/publish.yml/badge.svg)

a polyglot playground for experiments in application architecture, API design, library ergonomics, and developer tooling.

## Deployed Projects

| Project         | URL | Description                |
|-----------------|-----|----------------------------|
| **muchq**       | [muchq.com](https://muchq.com) | Project landing page       |
| **r3dr**        | [r3dr.net](https://r3dr.net) | Another URL shortener      |
| **1d4**         | [1d4.net](https://1d4.net) | Chess game indexer web UI  |
| **1d4.net MCP** | [mcp.1d4.net](https://mcp.1d4.net) | MCP server for chess tools |
| **microgpt**    | [tty1.uk](https://tty1.uk)     | Minimal GPT implementation |

## Repository Structure

The repository is organized into **domains**, each containing its own libraries, APIs, and applications.

- [**🎨 Graphics**](domains/graphics/README.md): Ray tracers, image processing, and rendering tools.
- [**🎮 Games**](domains/games/README.md): Game engines, chess tools, and multiplayer backends.
- [**🌐 Platform**](domains/platform/README.md): Core infrastructure, gRPC/HTTP wrappers, and utilities.
- [**🔗 r3dr**](domains/r3dr/README.md): URL shortening ecosystem.
- [**💬 Chat**](domains/chat/README.md): Real-time communication services.
- [**🤖 AI**](domains/ai/README.md): Neural network and machine learning experiments.
- [**🏆 Leet**](domains/leet/README.md): Algorithmic solutions and C utility libraries.

## Getting Started

- [**Build & IDE Instructions**](docs/BUILD_AND_IDE.md): How to build the project and set up your development environment.
- [**Importing Projects**](docs/IMPORTING.md): Guidelines for adding new projects to MoonBase.


<p align="center">
  <img src="domains/graphics/data/moon.gif" alt="MoonBase">
</p>

