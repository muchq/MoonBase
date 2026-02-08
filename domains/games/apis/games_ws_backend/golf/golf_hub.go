package golf

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"sync"
	"time"

	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/hub"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/players"
)

// GolfHub maintains active rooms and routes messages
type GolfHub struct {
	// Active rooms mapped by room ID
	rooms map[string]*Room

	// Client contexts mapping
	clientContexts map[*hub.Client]*ClientContext

	// Session token to context mapping for reconnection
	sessionTokens map[string]*ClientContext

	// Track which clients are authenticated (first-message auth)
	authenticatedClients map[*hub.Client]bool

	// Cleanup timers for disconnected players (key: sessionToken)
	cleanupTimers map[string]*time.Timer

	// Player ID generator
	idGenerator players.PlayerIDGenerator

	// Mutex for thread safety
	mu sync.RWMutex

	// Channels from hub interface
	gameMessage chan hub.GameMessageData
	register    chan *hub.Client
	unregister  chan *hub.Client
}

// NewGolfHub creates a new golf hub instance
func NewGolfHub(idGenerator players.PlayerIDGenerator) hub.Hub {
	return &GolfHub{
		rooms:                make(map[string]*Room),
		clientContexts:       make(map[*hub.Client]*ClientContext),
		sessionTokens:        make(map[string]*ClientContext),
		authenticatedClients: make(map[*hub.Client]bool),
		cleanupTimers:        make(map[string]*time.Timer),
		idGenerator:          idGenerator,
		gameMessage:          make(chan hub.GameMessageData),
		register:             make(chan *hub.Client),
		unregister:           make(chan *hub.Client),
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
	defer h.mu.Unlock()

	// Client is not authenticated yet - they must send an "authenticate" message first
	h.authenticatedClients[client] = false

	slog.Info("Golf client connected (unauthenticated)",
		"clientAddr", getClientAddr(client))
}

// handleAuthenticate processes first-message authentication
func (h *GolfHub) handleAuthenticate(client *hub.Client, sessionToken string) {
	h.mu.Lock()
	defer h.mu.Unlock()

	// Check if client is already authenticated
	if authenticated, exists := h.authenticatedClients[client]; exists && authenticated {
		h.sendErrorLocked(client, "Already authenticated")
		return
	}

	var ctx *ClientContext
	var reconnected bool

	if sessionToken != "" {
		// Reconnection attempt - validate token
		if err := ValidateSessionToken(sessionToken); err != nil {
			h.sendErrorLocked(client, fmt.Sprintf("Invalid session token: %v", err))
			return
		}

		// Look up existing context by token
		existingCtx, exists := h.sessionTokens[sessionToken]
		if !exists {
			h.sendErrorLocked(client, "Session not found - token may be expired")
			return
		}

		// Check if token is expired
		if time.Now().After(existingCtx.TokenExpiry) {
			// Clean up expired token
			delete(h.sessionTokens, sessionToken)
			h.sendErrorLocked(client, "Session token expired")
			return
		}

		// Cancel cleanup timer if exists
		if timer, exists := h.cleanupTimers[sessionToken]; exists {
			timer.Stop()
			delete(h.cleanupTimers, sessionToken)
		}

		// Reconnect player
		ctx = existingCtx
		reconnected = true

		// Update player connection status if in a room
		if ctx.RoomID != "" {
			if room, roomExists := h.rooms[ctx.RoomID]; roomExists {
				if player := room.GetPlayerByClientID(ctx.PlayerID); player != nil {
					player.IsConnected = true
					player.DisconnectedAt = nil
				}
			}
		}

		slog.Info("Player reconnected",
			"playerID", ctx.PlayerID,
			"roomID", ctx.RoomID,
			"gameID", ctx.GameID)
	} else {
		// New session - generate token and player ID
		token, err := CreateSessionToken()
		if err != nil {
			h.sendErrorLocked(client, "Failed to create session")
			slog.Error("Failed to create session token", "error", err)
			return
		}

		playerID := h.idGenerator.GenerateID()

		ctx = &ClientContext{
			PlayerID:     playerID,
			SessionToken: token.Token,
			TokenExpiry:  token.ExpiresAt,
			JoinedAt:     time.Now(),
			LastAction:   time.Now(),
		}

		// Store token mapping
		h.sessionTokens[token.Token] = ctx

		slog.Info("New session created",
			"playerID", playerID,
			"tokenExpiry", token.ExpiresAt)
	}

	// Associate context with client
	h.clientContexts[client] = ctx
	h.authenticatedClients[client] = true

	// Send authentication success message
	authMsg := AuthenticatedMessage{
		Type:         "authenticated",
		SessionToken: ctx.SessionToken,
		Reconnected:  reconnected,
	}
	h.sendMessageLocked(client, authMsg)
}

// handleUnregister processes client disconnection with reconnect grace period
func (h *GolfHub) handleUnregister(client *hub.Client) {
	h.mu.Lock()
	defer h.mu.Unlock()

	ctx, ok := h.clientContexts[client]
	if !ok {
		// Client not found - may not have authenticated
		delete(h.authenticatedClients, client)
		slog.Info("Unauthenticated client disconnected",
			"clientAddr", getClientAddr(client))
		return
	}

	sessionToken := ctx.SessionToken
	now := time.Now()

	// Mark player as disconnected if in a room
	var roomToUpdate *Room
	if ctx.RoomID != "" {
		if room, roomExists := h.rooms[ctx.RoomID]; roomExists {
			if player := room.GetPlayerByClientID(ctx.PlayerID); player != nil {
				player.IsConnected = false
				player.DisconnectedAt = &now
				roomToUpdate = room
			}
		}
	}

	// Remove client association but keep session token mapping for reconnection
	delete(h.clientContexts, client)
	delete(h.authenticatedClients, client)
	close(client.Send)

	slog.Info("Client disconnected - grace period started",
		"playerID", ctx.PlayerID,
		"roomID", ctx.RoomID,
		"gameID", ctx.GameID,
		"gracePeriod", ReconnectGracePeriod)

	// Schedule cleanup after grace period
	timer := time.AfterFunc(ReconnectGracePeriod, func() {
		h.cleanupDisconnectedPlayer(sessionToken)
	})
	h.cleanupTimers[sessionToken] = timer

	// Broadcast updated room state after releasing the lock
	if roomToUpdate != nil {
		h.mu.Unlock()
		h.broadcastRoomState(roomToUpdate)
		h.mu.Lock()
	}
}

// cleanupDisconnectedPlayer removes a player after reconnect grace period expires
func (h *GolfHub) cleanupDisconnectedPlayer(sessionToken string) {
	h.mu.Lock()
	defer h.mu.Unlock()

	// Remove cleanup timer
	delete(h.cleanupTimers, sessionToken)

	// Get context
	ctx, exists := h.sessionTokens[sessionToken]
	if !exists {
		return // Already cleaned up or reconnected
	}

	// Check if player reconnected
	for _, clientCtx := range h.clientContexts {
		if clientCtx.SessionToken == sessionToken {
			slog.Info("Player reconnected before cleanup",
				"playerID", ctx.PlayerID)
			return // Player reconnected, don't clean up
		}
	}

	var roomToUpdate *Room

	// Remove player from room if they're in one
	if ctx.RoomID != "" {
		if room, roomExists := h.rooms[ctx.RoomID]; roomExists {
			// Remove player from their current game if there is one
			if ctx.GameID != "" {
				if game, gameExists := room.Games[ctx.GameID]; gameExists {
					if err := game.RemovePlayer(ctx.PlayerID); err != nil {
						slog.Error("Failed to remove player from game",
							"error", err,
							"roomID", ctx.RoomID,
							"gameID", ctx.GameID)
					}
				}
			}

			// Remove player from room
			for i, player := range room.Players {
				if player.ClientID == ctx.PlayerID {
					room.Players = append(room.Players[:i], room.Players[i+1:]...)
					break
				}
			}

			room.LastActivity = time.Now()
			roomToUpdate = room

			// Clean up empty rooms
			if len(room.Players) == 0 {
				delete(h.rooms, ctx.RoomID)
				slog.Info("Removed empty room", "roomID", ctx.RoomID)
				roomToUpdate = nil // Don't broadcast for empty rooms
			}
		}
	}

	// Remove session token
	delete(h.sessionTokens, sessionToken)

	slog.Info("Player cleaned up after disconnect timeout",
		"playerID", ctx.PlayerID,
		"roomID", ctx.RoomID,
		"sessionToken", sessionToken[:16]+"...")

	// Broadcast updated room state after releasing the lock
	if roomToUpdate != nil {
		h.mu.Unlock()
		h.broadcastRoomState(roomToUpdate)
		h.mu.Lock()
	}
}

// handleGameMessage processes incoming game messages
func (h *GolfHub) handleGameMessage(msgData hub.GameMessageData) {
	msg, err := ParseIncomingMessage(msgData.Message)
	if err != nil {
		h.sendError(msgData.Sender, "Invalid message format")
		return
	}

	// Handle authentication message (doesn't require prior authentication)
	if msg.Type == "authenticate" {
		var authMsg AuthenticateMessage
		if err := json.Unmarshal(msgData.Message, &authMsg); err != nil {
			h.sendError(msgData.Sender, "Invalid authenticate message")
			return
		}
		h.handleAuthenticate(msgData.Sender, authMsg.SessionToken)
		return
	}

	// All other messages require authentication
	h.mu.RLock()
	authenticated := h.authenticatedClients[msgData.Sender]
	h.mu.RUnlock()

	if !authenticated {
		h.sendError(msgData.Sender, "Not authenticated - send authenticate message first")
		return
	}

	switch msg.Type {
	case "createRoom":
		h.handleCreateRoom(msgData.Sender)
	case "joinRoom":
		h.handleJoinRoom(msgData.Sender, msg.RoomID)
	case "leaveRoom":
		h.handleLeaveRoom(msgData.Sender, msg.RoomID)
	case "createGame":
		h.handleCreateGame(msgData.Sender, msg.RoomID)
	case "joinGame":
		h.handleJoinGame(msgData.Sender, msg.RoomID, msg.GameID)
	case "startGame":
		h.handleStartGame(msgData.Sender)
	case "startNewGame":
		h.handleStartNewGame(msgData.Sender)
	case "getRoomState":
		h.handleGetRoomState(msgData.Sender)
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

// handleCreateRoom creates a new room
func (h *GolfHub) handleCreateRoom(client *hub.Client) {
	h.mu.Lock()
	defer h.mu.Unlock()

	// Check if client is already in a room
	ctx := h.clientContexts[client]
	if ctx != nil && ctx.RoomID != "" {
		h.sendError(client, "Already in a room")
		return
	}

	// Create new room
	room := h.createRoom(client)
	h.rooms[room.ID] = room

	// Update existing context (authenticated) with room info
	if ctx := h.clientContexts[client]; ctx != nil {
		ctx.RoomID = room.ID
		ctx.GameID = "" // Not in a specific game yet
		ctx.LastAction = time.Now()
	}

	// Send room joined message with session token
	player := room.Players[0] // First player is the creator
	h.sendRoomJoined(client, player.ID, ctx.SessionToken, room)

	slog.Info("Room created",
		"roomID", room.ID,
		"playerID", player.ID,
		"clientAddr", getClientAddr(client))
}

// handleJoinRoom joins an existing room
func (h *GolfHub) handleJoinRoom(client *hub.Client, roomID string) {
	var room *Room
	var player *Player

	// Do the joining logic with the lock
	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		// Validate required parameters
		if roomID == "" {
			h.sendError(client, "Room ID is required")
			return
		}

		// Check if client is already in a room
		ctx := h.clientContexts[client]
		if ctx != nil && ctx.RoomID != "" {
			// If already in the same room, return error
			if ctx.RoomID == roomID {
				h.sendError(client, "player already in room")
				return
			} else {
				h.sendError(client, "Already in a different room")
				return
			}
		} else {
			// Add player to room (new player)
			var err error
			player, err = h.addPlayerToRoom(roomID, client)
			if err != nil {
				h.sendError(client, err.Error())
				return
			}
			room = h.rooms[roomID]

			// Update existing context (authenticated) with room info
			if ctx := h.clientContexts[client]; ctx != nil {
				ctx.RoomID = roomID
				ctx.GameID = "" // Not in a specific game yet
				ctx.LastAction = time.Now()
			}
		}
	}()

	// Exit if join failed
	if room == nil || player == nil {
		return
	}

	// Get session token from context
	h.mu.RLock()
	ctx := h.clientContexts[client]
	sessionToken := ""
	if ctx != nil {
		sessionToken = ctx.SessionToken
	}
	h.mu.RUnlock()

	// Send room joined message to new player
	h.sendRoomJoined(client, player.ID, sessionToken, room)

	// Broadcast updated state to all players in room
	h.broadcastRoomState(room)

	slog.Info("Player joined room and game",
		"roomID", roomID,
		"playerID", player.ID,
		"clientAddr", getClientAddr(client))
}

// handleLeaveRoom leaves an existing room
func (h *GolfHub) handleLeaveRoom(client *hub.Client, roomID string) {
	var room *Room

	// Do the leaving logic with the lock
	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		// Validate required parameters
		if roomID == "" {
			h.sendError(client, "Room ID is required")
			return
		}

		// Check if client is in the specified room
		ctx := h.clientContexts[client]
		if ctx == nil || ctx.RoomID != roomID {
			h.sendError(client, "Not in the specified room")
			return
		}

		room = h.rooms[roomID]
		if room == nil {
			h.sendError(client, "Room not found")
			return
		}

		// Mark player as disconnected but keep in room history
		clientID := getClientID(client)
		for _, player := range room.Players {
			if player.ClientID == clientID {
				player.IsConnected = false
				break
			}
		}

		// Remove from active game if in one
		if ctx.GameID != "" {
			if game, exists := room.Games[ctx.GameID]; exists {
				game.RemovePlayer(clientID)
			}
		}

		// Clear client context
		delete(h.clientContexts, client)
		room.LastActivity = time.Now()
	}()

	// Broadcast updated room state if room still exists
	if room != nil {
		h.broadcastRoomState(room)
	}

	slog.Info("Player left room",
		"roomID", roomID,
		"clientAddr", getClientAddr(client))
}

// handleCreateGame creates a new game within an existing room
func (h *GolfHub) handleCreateGame(client *hub.Client, roomID string) {
	var room *Room
	var game *Game
	var player *Player
	var gameState *GameState

	clientID := getClientID(client)

	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		// Validate roomID
		if roomID == "" {
			h.sendError(client, "Room ID is required")
			return
		}

		// Check if room exists
		var exists bool
		room, exists = h.rooms[roomID]
		if !exists {
			h.sendError(client, "Room not found")
			return
		}

		// Check if client is in this room
		ctx := h.clientContexts[client]
		if ctx == nil || ctx.RoomID != roomID {
			h.sendError(client, "Must be in the room to create a game")
			return
		}

		// Generate a unique game ID for this room
		gameID := h.generateGameID(room)

		// Create the game using existing helper
		var err error
		game, err = h.createGameInRoom(roomID, gameID)
		if err != nil {
			h.sendError(client, err.Error())
			return
		}

		// Add client to the new game using persistent player ID and name
		if ctx.PlayerID == "" {
			h.sendError(client, "client context missing player ID")
			return
		}
		
		player, err = game.AddPlayer(clientID, ctx.PlayerID, ctx.PlayerID)
		if err != nil {
			h.sendError(client, err.Error())
			return
		}

		// Update client's game context
		ctx.GameID = gameID
		ctx.PlayerID = player.ID
		ctx.LastAction = time.Now()

		room.LastActivity = time.Now()

		// Get the game state while we still hold the lock to ensure consistency
		gameState = game.GetStateForPlayer(clientID)

		slog.Info("Game created in room",
			"roomID", roomID,
			"gameID", gameID,
			"playerID", player.ID,
			"clientAddr", getClientAddr(client))

		// Broadcast room state update to show new game while we hold the lock
		// This ensures the room state includes the updated game with the player
		h.broadcastRoomStateLocked(room)
	}()

	// Exit early if we failed to create game/player
	if room == nil || game == nil || player == nil || gameState == nil {
		return
	}

	// Send game joined message
	h.sendGameJoined(client, player.ID, gameState)

	// Broadcast updated game state to all players in the game
	h.broadcastGameState(game)
}

// generateGameID generates a unique game ID within a room
func (h *GolfHub) generateGameID(room *Room) string {
	for attempts := 0; attempts < 10; attempts++ {
		gameID := GenerateGameID()
		if _, exists := room.Games[gameID]; !exists {
			return gameID
		}
	}
	// Fallback if we somehow have collisions (very unlikely)
	return fmt.Sprintf("%s_%d", GenerateGameID(), time.Now().UnixNano())
}

// handleJoinGame joins an existing room and specific game
func (h *GolfHub) handleJoinGame(client *hub.Client, roomID string, gameID string) {
	var room *Room
	var game *Game
	var player *Player
	var err error

	// Do the joining logic with the lock
	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		// Validate required parameters
		if roomID == "" {
			h.sendError(client, "Room ID is required")
			return
		}
		if gameID == "" {
			h.sendError(client, "Game ID is required")
			return
		}

		// Check if client is in the room
		ctx := h.clientContexts[client]
		if ctx == nil || ctx.RoomID != roomID {
			h.sendError(client, "Player not found in room")
			return
		}

		// Get the room from the rooms map
		var roomExists bool
		room, roomExists = h.rooms[roomID]
		if !roomExists {
			h.sendError(client, "Room not found")
			return
		}

		// Check if game exists
		var gameExists bool
		game, gameExists = room.Games[gameID]
		if !gameExists {
			h.sendError(client, "Game does not exist in room")
			return
		}

		clientID := getClientID(client)
		
		// Use persistent player ID and name from context
		if ctx.PlayerID == "" {
			h.sendError(client, "client context missing player ID")
			return
		}
		
		player, err = game.AddPlayer(clientID, ctx.PlayerID, ctx.PlayerID)
		if err != nil {
			h.sendError(client, err.Error())
			return
		}

		// Update client's game context
		ctx.GameID = gameID
		ctx.PlayerID = player.ID
		ctx.LastAction = time.Now()
	}()

	// Exit if join failed
	if room == nil || game == nil || player == nil {
		return
	}

	// Send game joined message to the player who just joined
	clientID := getClientID(client)
	h.sendGameJoined(client, player.ID, game.GetStateForPlayer(clientID))

	// Broadcast updated game state to all players in the game
	h.broadcastGameState(game)

	// Broadcast updated state to all players in room
	h.broadcastRoomState(room)

	slog.Info("Player joined game",
		"roomID", roomID,
		"gameID", gameID,
		"playerID", player.ID,
		"clientAddr", getClientAddr(client))
}

// handleStartGame starts a specific game within the current room
func (h *GolfHub) handleStartGame(client *hub.Client) {
	h.mu.Lock()
	ctx := h.clientContexts[client]
	if ctx == nil {
		h.mu.Unlock()
		h.sendError(client, "Not in a room")
		return
	}

	room := h.rooms[ctx.RoomID]
	if room == nil {
		h.mu.Unlock()
		h.sendError(client, "Room not found")
		return
	}

	if ctx.GameID == "" {
		h.mu.Unlock()
		h.sendError(client, "Not in a specific game")
		return
	}

	game := room.Games[ctx.GameID]
	if game == nil {
		h.mu.Unlock()
		h.sendError(client, "Game not found")
		return
	}

	h.mu.Unlock()

	if err := game.StartGame(); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast game started message
	h.broadcastToGameLocked(game, &GameStartedMessage{Type: "gameStarted"})

	// Broadcast updated game state
	h.broadcastGameState(game)

	slog.Info("Game started", "gameID", game.state.ID, "roomID", ctx.RoomID)
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
		h.handleGameEnded(game)
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
		h.handleGameEnded(game)
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
	ctx := h.clientContexts[client]
	if ctx == nil || ctx.GameID == "" {
		return nil
	}

	room := h.rooms[ctx.RoomID]
	if room == nil {
		return nil
	}

	return room.Games[ctx.GameID]
}

// getClientRoom returns the room for a given client using ClientContext
func (h *GolfHub) getClientRoom(client *hub.Client) *Room {
	ctx := h.clientContexts[client]
	if ctx == nil || ctx.RoomID == "" {
		return nil
	}
	return h.rooms[ctx.RoomID]
}

// createGameInRoom creates a new game within a room
func (h *GolfHub) createGameInRoom(roomID string, gameID string) (*Game, error) {
	room, exists := h.rooms[roomID]
	if !exists {
		return nil, fmt.Errorf("room not found")
	}

	connectedPlayers := make([]*Player, 0)

	// Create new game
	idGenerator := &players.WhimsicalIDGenerator{}
	game := NewGameInRoom(gameID, roomID, connectedPlayers, idGenerator)

	room.Games[gameID] = game
	room.LastActivity = time.Now()

	return game, nil
}

// removeGameFromRoom removes a completed game from the room
func (h *GolfHub) removeGameFromRoom(roomID string, gameID string) error {
	room, exists := h.rooms[roomID]
	if !exists {
		return fmt.Errorf("room not found")
	}

	game, exists := room.Games[gameID]
	if !exists {
		return fmt.Errorf("game not found")
	}

	// Only remove if game is ended
	if game.state.GamePhase == "ended" {
		delete(room.Games, gameID)
		room.LastActivity = time.Now()
	}

	return nil
}

func (h *GolfHub) sendError(client *hub.Client, message string) {
	msg := &ErrorMessage{
		Type:    "error",
		Message: message,
	}
	h.sendJSON(client, msg)
}

// sendErrorLocked sends an error when lock is already held
func (h *GolfHub) sendErrorLocked(client *hub.Client, message string) {
	msg := &ErrorMessage{
		Type:    "error",
		Message: message,
	}
	h.sendJSONLocked(client, msg)
}

// sendMessageLocked sends a message when lock is already held
func (h *GolfHub) sendMessageLocked(client *hub.Client, message interface{}) {
	h.sendJSONLocked(client, message)
}

// sendJSONLocked sends JSON when lock is already held
func (h *GolfHub) sendJSONLocked(client *hub.Client, message interface{}) {
	data, err := json.Marshal(message)
	if err != nil {
		slog.Error("Failed to marshal message",
			"error", err,
			"type", message)
		return
	}

	select {
	case client.Send <- data:
		// Successfully sent message
	default:
		// Buffer full - force disconnect
		slog.Warn("Client send buffer full, forcing disconnect",
			"clientAddr", getClientAddr(client),
			"messageType", fmt.Sprintf("%T", message))
		close(client.Send)
		delete(h.clientContexts, client)
	}
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
	for client, ctx := range h.clientContexts {
		if ctx != nil && ctx.GameID == game.state.ID {
			if room, exists := h.rooms[ctx.RoomID]; exists {
				if gameInRoom, gameExists := room.Games[ctx.GameID]; gameExists && gameInRoom.state.ID == game.state.ID {
					personalizedState := game.GetStateForPlayer(getClientID(client))
					pairs = append(pairs, clientStatePair{client: client, state: personalizedState})
				}
			}
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

// handleGameEnded processes game completion and updates room statistics
func (h *GolfHub) handleGameEnded(game *Game) {
	// First broadcast the game ended message
	h.broadcastGameEnded(game)

	// If this game belongs to a room, update room statistics
	roomID := game.GetRoomID()
	if roomID != "" {
		h.mu.Lock()
		err := h.completeGameInRoom(roomID, game.state.ID)
		if err != nil {
			slog.Error("Failed to complete game in room",
				"error", err,
				"roomID", roomID,
				"gameID", game.state.ID)
		} else {
			// Broadcast updated room state
			if room, exists := h.rooms[roomID]; exists {
				h.mu.Unlock()
				h.broadcastRoomState(room)

				slog.Info("Game completed and room stats updated",
					"roomID", roomID,
					"gameID", game.state.ID)
				return
			}
		}
		h.mu.Unlock()
	}
}

func (h *GolfHub) broadcastToGameLocked(game *Game, message interface{}) {
	for client, ctx := range h.clientContexts {
		if ctx != nil && ctx.GameID == game.state.ID {
			if room, exists := h.rooms[ctx.RoomID]; exists {
				if gameInRoom, gameExists := room.Games[ctx.GameID]; gameExists && gameInRoom.state.ID == game.state.ID {
					h.sendJSON(client, message)
				}
			}
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
		// Successfully sent message
	default:
		// Buffer full - force disconnect
		slog.Warn("Client send buffer full, forcing disconnect",
			"clientAddr", getClientAddr(client),
			"messageType", fmt.Sprintf("%T", message))
		close(client.Send)
		h.mu.Lock()
		delete(h.clientContexts, client)
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

// Room Management Methods

// createRoom creates a new room with the given client as the first player
func (h *GolfHub) createRoom(client *hub.Client) *Room {
	roomID := GenerateRoomID()

	// Use persistent player ID from context
	ctx := h.clientContexts[client]
	if ctx == nil || ctx.PlayerID == "" {
		// This should not happen if client was properly registered
		return nil
	}

	player := &Player{
		ID:            ctx.PlayerID,
		Name:          ctx.PlayerID,
		ClientID:      ctx.PlayerID, // Use stable player ID for reconnection
		Cards:         CreateHiddenCards(),
		Score:         0,
		RevealedCards: make([]int, 0),
		IsReady:       false,
		HasPeeked:     false,
		TotalScore:    0,
		GamesPlayed:   0,
		GamesWon:      0,
		IsConnected:   true,
		JoinedAt:      time.Now(),
	}

	room := &Room{
		ID:           roomID,
		Players:      []*Player{player},
		Games:        make(map[string]*Game),
		GameHistory:  make([]*GameResult, 0),
		CreatedAt:    time.Now(),
		LastActivity: time.Now(),
	}

	return room
}

// addPlayerToRoom adds a player to an existing room
func (h *GolfHub) addPlayerToRoom(roomID string, client *hub.Client) (*Player, error) {
	room, exists := h.rooms[roomID]
	if !exists {
		return nil, fmt.Errorf("room not found")
	}

	if len(room.Players) >= 4 {
		return nil, fmt.Errorf("room is full")
	}

	// Create new player using persistent player ID from context
	ctx := h.clientContexts[client]
	if ctx == nil || ctx.PlayerID == "" {
		return nil, fmt.Errorf("client context not found or missing player ID")
	}

	// Check if player is already in room (by stable player ID)
	for _, player := range room.Players {
		if player.ClientID == ctx.PlayerID {
			player.IsConnected = true
			room.LastActivity = time.Now()
			return player, nil
		}
	}

	player := &Player{
		ID:            ctx.PlayerID,
		Name:          ctx.PlayerID,
		ClientID:      ctx.PlayerID, // Use stable player ID for reconnection
		Cards:         CreateHiddenCards(),
		Score:         0,
		RevealedCards: make([]int, 0),
		IsReady:       false,
		HasPeeked:     false,
		TotalScore:    0,
		GamesPlayed:   0,
		GamesWon:      0,
		IsConnected:   true,
		JoinedAt:      time.Now(),
	}

	room.Players = append(room.Players, player)
	room.LastActivity = time.Now()

	return player, nil
}

// startNewGameInRoom creates a new game within a room with a generated game ID
func (h *GolfHub) startNewGameInRoom(roomID string) (*Game, error) {
	gameID := GenerateGameID()
	return h.createGameInRoom(roomID, gameID)
}

// completeGameInRoom handles game completion and updates room stats
func (h *GolfHub) completeGameInRoom(roomID string, gameID string) error {
	room, exists := h.rooms[roomID]
	if !exists {
		return fmt.Errorf("room not found")
	}

	game, exists := room.Games[gameID]
	if !exists {
		return fmt.Errorf("game not found")
	}

	if game.state.GamePhase != "ended" {
		return fmt.Errorf("game is not completed")
	}

	gameResult := game.GetGameResult()
	if gameResult == nil {
		return fmt.Errorf("failed to get game result")
	}

	// Update room history
	room.GameHistory = append(room.GameHistory, gameResult)
	room.LastActivity = time.Now()

	// Update player statistics
	for _, finalScore := range gameResult.FinalScores {
		for _, roomPlayer := range room.Players {
			if roomPlayer.Name == finalScore.PlayerName {
				roomPlayer.TotalScore += finalScore.Score
				roomPlayer.GamesPlayed++
				if finalScore.PlayerName == gameResult.Winner {
					roomPlayer.GamesWon++
				}
				break
			}
		}
	}

	// Remove completed game from active games
	delete(room.Games, gameID)

	return nil
}

// broadcastRoomState broadcasts room state to all players in the room
func (h *GolfHub) broadcastRoomState(room *Room) {
	// Create a slice to hold client-room pairs to avoid holding lock during sends
	type clientRoomPair struct {
		client *hub.Client
		room   *Room
	}
	var pairs []clientRoomPair

	// Collect all clients in this room
	h.mu.RLock()
	for client, ctx := range h.clientContexts {
		if ctx != nil && ctx.RoomID == room.ID {
			pairs = append(pairs, clientRoomPair{client: client, room: room})
		}
	}
	h.mu.RUnlock()

	// Send messages without holding the lock
	for _, pair := range pairs {
		msg := &RoomStateUpdateMessage{
			Type:      "roomStateUpdate",
			RoomState: pair.room,
		}
		h.sendJSON(pair.client, msg)
	}
}

// broadcastRoomStateLocked broadcasts room state to all players in the room
// This version assumes the caller already holds the mutex
func (h *GolfHub) broadcastRoomStateLocked(room *Room) {
	// Create a slice to hold clients
	var clients []*hub.Client

	// Collect all clients in this room (we already hold the lock)
	for client, ctx := range h.clientContexts {
		if ctx != nil && ctx.RoomID == room.ID {
			clients = append(clients, client)
		}
	}

	// Pre-serialize the room state while holding the lock
	msg := &RoomStateUpdateMessage{
		Type:      "roomStateUpdate",
		RoomState: room,
	}
	data, err := json.Marshal(msg)
	if err != nil {
		slog.Error("Failed to marshal room state message",
			"error", err,
			"roomID", room.ID)
		return
	}

	// Send the pre-serialized message to all clients
	for _, client := range clients {
		// Temporarily release the lock to send the message
		h.mu.Unlock()
		select {
		case client.Send <- data:
		default:
			close(client.Send)
			// We can't safely delete from clientContexts here since we don't have the lock
			// This will be cleaned up by handleUnregister
		}
		h.mu.Lock()
	}
}

// New Room-Based Message Handlers

// handleStartNewGame starts a new game within the current room
func (h *GolfHub) handleStartNewGame(client *hub.Client) {
	h.mu.Lock()
	room := h.getClientRoom(client)
	if room == nil {
		h.mu.Unlock()
		h.sendError(client, "Not in a room")
		return
	}

	// Get the previous game ID from client context
	ctx := h.clientContexts[client]
	previousGameID := ""
	if ctx != nil {
		previousGameID = ctx.GameID
	}

	// Start new game in room
	game, err := h.startNewGameInRoom(room.ID)
	if err != nil {
		h.mu.Unlock()
		h.sendError(client, err.Error())
		return
	}
	h.mu.Unlock()

	// Broadcast new game started message with game IDs
	h.broadcastToRoom(room, &NewGameStartedMessage{
		Type:           "newGameStarted",
		GameID:         game.state.ID,
		PreviousGameID: previousGameID,
	})

	// Broadcast updated room state
	h.broadcastRoomState(room)

	slog.Info("New game started in room",
		"roomID", room.ID,
		"gameID", game.state.ID,
		"previousGameID", previousGameID)
}

// handleGetRoomState sends the current room state to the client
func (h *GolfHub) handleGetRoomState(client *hub.Client) {
	h.mu.RLock()
	room := h.getClientRoom(client)
	h.mu.RUnlock()

	if room == nil {
		h.sendError(client, "Not in a room")
		return
	}

	// Send room state update
	msg := &RoomStateUpdateMessage{
		Type:      "roomStateUpdate",
		RoomState: room,
	}
	h.sendJSON(client, msg)
}

// sendRoomJoined sends room joined message to client
func (h *GolfHub) sendRoomJoined(client *hub.Client, playerID string, sessionToken string, room *Room) {
	msg := &RoomJoinedMessage{
		Type:         "roomJoined",
		PlayerID:     playerID,
		SessionToken: sessionToken,
		RoomState:    room,
	}
	h.sendJSON(client, msg)
}

// broadcastToRoom broadcasts a message to all clients in a room
func (h *GolfHub) broadcastToRoom(room *Room, message interface{}) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	for client, ctx := range h.clientContexts {
		if ctx != nil && ctx.RoomID == room.ID {
			h.sendJSON(client, message)
		}
	}
}
