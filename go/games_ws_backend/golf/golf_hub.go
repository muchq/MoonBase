package golf

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"sync"

	"github.com/muchq/moonbase/go/games_ws_backend/hub"
	"github.com/muchq/moonbase/go/games_ws_backend/players"
)

// GolfHub maintains active games and routes messages
type GolfHub struct {
	// Active games mapped by game ID
	games map[string]*Game

	// Client to game ID mapping
	clientToGame map[*hub.Client]string

	// Mutex for thread safety
	mu sync.RWMutex

	// Channels from hub interface
	gameMessage chan hub.GameMessageData
	register    chan *hub.Client
	unregister  chan *hub.Client

	// Registered clients
	clients map[*hub.Client]bool
}

// NewGolfHub creates a new golf hub instance
func NewGolfHub() hub.Hub {
	return &GolfHub{
		games:        make(map[string]*Game),
		clientToGame: make(map[*hub.Client]string),
		gameMessage:  make(chan hub.GameMessageData),
		register:     make(chan *hub.Client),
		unregister:   make(chan *hub.Client),
		clients:      make(map[*hub.Client]bool),
	}
}

// Register handles client registration
func (h *GolfHub) Register(c *hub.Client) {
	h.register <- c
}

// Unregister handles client unregistration
func (h *GolfHub) Unregister(c *hub.Client) {
	h.unregister <- c
}

// GameMessage handles incoming game messages
func (h *GolfHub) GameMessage(data hub.GameMessageData) {
	h.gameMessage <- data
}

// Run starts the hub's main event loop
func (h *GolfHub) Run() {
	for {
		select {
		case client := <-h.register:
			h.handleRegister(client)

		case client := <-h.unregister:
			h.handleUnregister(client)

		case msgData := <-h.gameMessage:
			h.handleGameMessage(msgData)
		}
	}
}

// handleRegister processes client registration
func (h *GolfHub) handleRegister(client *hub.Client) {
	h.mu.Lock()
	h.clients[client] = true
	h.mu.Unlock()

	slog.Info("Golf client connected",
		"clientAddr", getClientAddr(client))
}

// handleUnregister processes client disconnection
func (h *GolfHub) handleUnregister(client *hub.Client) {
	var gameToUpdate *Game

	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		if _, ok := h.clients[client]; ok {
			// Remove player from game if they're in one
			if gameID, exists := h.clientToGame[client]; exists {
				if game, gameExists := h.games[gameID]; gameExists {
					if err := game.RemovePlayer(getClientID(client)); err != nil {
						slog.Error("Failed to remove player from game",
							"error", err,
							"gameID", gameID)
					} else {
						// Store game reference for broadcasting after releasing lock
						gameToUpdate = game

						// Clean up empty games
						if len(game.state.Players) == 0 {
							delete(h.games, gameID)
							slog.Info("Removed empty game", "gameID", gameID)
							gameToUpdate = nil // Don't broadcast for empty games
						}
					}
				}
				delete(h.clientToGame, client)
			}

			delete(h.clients, client)
			close(client.Send)

			slog.Info("Golf client disconnected",
				"clientAddr", getClientAddr(client))
		}
	}()

	// Broadcast updated game state after releasing the lock
	if gameToUpdate != nil {
		h.broadcastGameState(gameToUpdate)
	}
}

// handleGameMessage processes incoming game messages
func (h *GolfHub) handleGameMessage(msgData hub.GameMessageData) {
	msg, err := ParseIncomingMessage(msgData.Message)
	if err != nil {
		h.sendError(msgData.Sender, "Invalid message format")
		return
	}

	switch msg.Type {
	case "createGame":
		h.handleCreateGame(msgData.Sender)
	case "joinGame":
		h.handleJoinGame(msgData.Sender, msg.GameID)
	case "startGame":
		h.handleStartGame(msgData.Sender)
	case "peekCard":
		h.handlePeekCard(msgData.Sender, msg.CardIndex)
	case "drawCard":
		h.handleDrawCard(msgData.Sender)
	case "takeFromDiscard":
		h.handleTakeFromDiscard(msgData.Sender)
	case "swapCard":
		h.handleSwapCard(msgData.Sender, msg.CardIndex)
	case "discardDrawn":
		h.handleDiscardDrawn(msgData.Sender)
	case "knock":
		h.handleKnock(msgData.Sender)
	case "hideCards":
		h.handleHideCards(msgData.Sender)
	default:
		h.sendError(msgData.Sender, "Unknown message type: "+msg.Type)
	}
}

// handleCreateGame creates a new game
func (h *GolfHub) handleCreateGame(client *hub.Client) {
	h.mu.Lock()
	defer h.mu.Unlock()

	// Check if client is already in a game
	if _, exists := h.clientToGame[client]; exists {
		h.sendError(client, "Already in a game")
		return
	}

	// Create new game
	gameID := GenerateGameID()
	idGenerator := &players.WhimsicalIDGenerator{}
	game := NewGame(gameID, idGenerator)
	h.games[gameID] = game

	// Add player to game
	player, err := game.AddPlayer(getClientID(client))
	if err != nil {
		h.sendError(client, err.Error())
		delete(h.games, gameID)
		return
	}

	h.clientToGame[client] = gameID

	// Send game joined message with personalized view
	h.sendGameJoined(client, player.ID, game.GetStateForPlayer(getClientID(client)))

	slog.Info("Game created",
		"gameID", gameID,
		"playerID", player.ID,
		"clientAddr", getClientAddr(client))
}

// handleJoinGame joins an existing game
func (h *GolfHub) handleJoinGame(client *hub.Client, gameID string) {
	var game *Game
	var player *Player

	// Do the joining logic with the lock
	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		// Check if client is already in a game
		if _, exists := h.clientToGame[client]; exists {
			h.sendError(client, "Already in a game")
			return
		}

		// Find game
		var exists bool
		game, exists = h.games[gameID]
		if !exists {
			h.sendError(client, "Game not found")
			return
		}

		// Add player to game
		var err error
		player, err = game.AddPlayer(getClientID(client))
		if err != nil {
			h.sendError(client, err.Error())
			return
		}

		h.clientToGame[client] = gameID
	}()

	// Exit if join failed
	if game == nil || player == nil {
		return
	}

	// Send game joined message to new player with personalized view
	h.sendGameJoined(client, player.ID, game.GetStateForPlayer(getClientID(client)))

	// Broadcast updated state to all players (without holding the lock)
	h.broadcastGameState(game)

	slog.Info("Player joined game",
		"gameID", gameID,
		"playerID", player.ID,
		"clientAddr", getClientAddr(client))
}

// handleStartGame starts the game
func (h *GolfHub) handleStartGame(client *hub.Client) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	if err := game.StartGame(); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast game started message
	h.broadcastToGameLocked(game, &GameStartedMessage{Type: "gameStarted"})

	// Broadcast updated game state
	h.broadcastGameState(game)

	slog.Info("Game started", "gameID", game.state.ID)
}

// handlePeekCard handles card peeking
func (h *GolfHub) handlePeekCard(client *hub.Client, cardIndex int) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	if err := game.PeekCard(getClientID(client), cardIndex); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Send updated game state to all players if all have peeked
	if game.state.GamePhase == "peeking" {
		h.broadcastGameState(game)
	} else {
		// Send updated game state only to the peeking player with their personalized view
		h.sendGameState(client, game.GetStateForPlayer(getClientID(client)))
	}
}

// handleDrawCard handles drawing from deck
func (h *GolfHub) handleDrawCard(client *hub.Client) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	if err := game.DrawCard(getClientID(client)); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast updated state
	h.broadcastGameState(game)
}

// handleTakeFromDiscard handles taking from discard pile
func (h *GolfHub) handleTakeFromDiscard(client *hub.Client) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	if err := game.TakeFromDiscard(getClientID(client)); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast updated state
	h.broadcastGameState(game)
}

// handleSwapCard handles card swapping
func (h *GolfHub) handleSwapCard(client *hub.Client, cardIndex int) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	oldPlayerIndex := game.state.CurrentPlayerIndex

	if err := game.SwapCard(getClientID(client), cardIndex); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast updated state
	h.broadcastGameState(game)

	// Check if turn changed
	if oldPlayerIndex != game.state.CurrentPlayerIndex {
		h.broadcastTurnChanged(game)
	}

	// Check if game ended
	if game.state.GamePhase == "ended" {
		h.broadcastGameEnded(game)
	}
}

// handleDiscardDrawn handles discarding the drawn card
func (h *GolfHub) handleDiscardDrawn(client *hub.Client) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	oldPlayerIndex := game.state.CurrentPlayerIndex

	if err := game.DiscardDrawn(getClientID(client)); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast updated state
	h.broadcastGameState(game)

	// Check if turn changed
	if oldPlayerIndex != game.state.CurrentPlayerIndex {
		h.broadcastTurnChanged(game)
	}

	// Check if game ended
	if game.state.GamePhase == "ended" {
		h.broadcastGameEnded(game)
	}
}

// handleKnock handles knocking
func (h *GolfHub) handleKnock(client *hub.Client) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	player := game.GetPlayerByClientID(getClientID(client))
	if player == nil {
		h.sendError(client, "Player not found")
		return
	}

	if err := game.Knock(getClientID(client)); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast player knocked message
	h.broadcastToGameLocked(game, &PlayerKnockedMessage{
		Type:       "playerKnocked",
		PlayerName: player.Name,
	})

	// Broadcast updated state
	h.broadcastGameState(game)
}

// Helper methods

func (h *GolfHub) getClientGame(client *hub.Client) *Game {
	gameID, exists := h.clientToGame[client]
	if !exists {
		return nil
	}
	return h.games[gameID]
}

func (h *GolfHub) sendError(client *hub.Client, message string) {
	msg := &ErrorMessage{
		Type:    "error",
		Message: message,
	}
	h.sendJSON(client, msg)
}

func (h *GolfHub) sendGameJoined(client *hub.Client, playerID string, state *GameState) {
	msg := &GameJoinedMessage{
		Type:      "gameJoined",
		PlayerID:  playerID,
		GameState: state,
	}
	h.sendJSON(client, msg)
}

func (h *GolfHub) sendGameState(client *hub.Client, state *GameState) {
	msg := &GameStateUpdateMessage{
		Type:      "gameState",
		GameState: state,
	}
	h.sendJSON(client, msg)
}

func (h *GolfHub) broadcastGameState(game *Game) {
	// Create a slice to hold client-state pairs to avoid holding lock during sends
	type clientStatePair struct {
		client *hub.Client
		state  *GameState
	}
	var pairs []clientStatePair

	// Collect all clients and their personalized states
	h.mu.RLock()
	for client, gameID := range h.clientToGame {
		if gameID == game.state.ID {
			personalizedState := game.GetStateForPlayer(getClientID(client))
			pairs = append(pairs, clientStatePair{client: client, state: personalizedState})
		}
	}
	h.mu.RUnlock()

	// Send messages without holding the lock
	for _, pair := range pairs {
		msg := &GameStateUpdateMessage{
			Type:      "gameState",
			GameState: pair.state,
		}
		h.sendJSON(pair.client, msg)
	}
}

func (h *GolfHub) broadcastTurnChanged(game *Game) {
	if len(game.state.Players) == 0 {
		return
	}
	currentPlayer := game.state.Players[game.state.CurrentPlayerIndex]
	msg := &TurnChangedMessage{
		Type:       "turnChanged",
		PlayerName: currentPlayer.Name,
	}
	h.broadcastToGameLocked(game, msg)
}

func (h *GolfHub) broadcastGameEnded(game *Game) {
	winner := game.GetWinner()
	if winner == nil {
		return
	}

	msg := &GameEndedMessage{
		Type:        "gameEnded",
		Winner:      winner.Name,
		FinalScores: game.GetFinalScores(),
	}
	h.broadcastToGameLocked(game, msg)
}

func (h *GolfHub) broadcastToGame(game *Game, message interface{}) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	h.broadcastToGameLocked(game, message)
}

func (h *GolfHub) broadcastToGameLocked(game *Game, message interface{}) {
	for client, gameID := range h.clientToGame {
		if gameID == game.state.ID {
			h.sendJSON(client, message)
		}
	}
}

func (h *GolfHub) sendJSON(client *hub.Client, message interface{}) {
	data, err := json.Marshal(message)
	if err != nil {
		slog.Error("Failed to marshal message",
			"error", err,
			"type", message)
		return
	}

	select {
	case client.Send <- data:
	default:
		close(client.Send)
		h.mu.Lock()
		delete(h.clients, client)
		h.mu.Unlock()
	}
}

// handleHideCards handles hiding cards after peek countdown
func (h *GolfHub) handleHideCards(client *hub.Client) {
	h.mu.RLock()
	game := h.getClientGame(client)
	h.mu.RUnlock()

	if game == nil {
		h.sendError(client, "Not in a game")
		return
	}

	// Hide the cards
	game.HidePeekedCards()

	// Broadcast updated state to all players
	h.broadcastGameState(game)
}

// Utility functions

func getClientID(client *hub.Client) string {
	if client.Conn != nil {
		return client.Conn.RemoteAddr().String()
	}
	// For testing: use the client's memory address as a unique ID
	return fmt.Sprintf("test-client-%p", client)
}

func getClientAddr(client *hub.Client) string {
	if client.Conn != nil {
		return client.Conn.RemoteAddr().String()
	}
	return "test-client"
}
