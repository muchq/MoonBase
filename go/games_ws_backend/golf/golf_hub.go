package golf

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"sync"
	"time"

	"github.com/muchq/moonbase/go/games_ws_backend/hub"
	"github.com/muchq/moonbase/go/games_ws_backend/players"
)

// GolfHub maintains active rooms and routes messages
type GolfHub struct {
	// Active rooms mapped by room ID
	rooms map[string]*Room

	// Client to room ID mapping
	clientToRoom map[*hub.Client]string

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
		rooms:        make(map[string]*Room),
		clientToRoom: make(map[*hub.Client]string),
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
	var roomToUpdate *Room

	func() {
		h.mu.Lock()
		defer h.mu.Unlock()

		if _, ok := h.clients[client]; ok {
			// Remove player from room if they're in one
			if roomID, exists := h.clientToRoom[client]; exists {
				if room, roomExists := h.rooms[roomID]; roomExists {
					clientID := getClientID(client)
					
					// Mark player as disconnected
					for _, player := range room.Players {
						if player.ClientID == clientID {
							player.IsConnected = false
							break
						}
					}
					
					// Remove player from current game if there is one
					if room.CurrentGame != nil {
						if err := room.CurrentGame.RemovePlayer(clientID); err != nil {
							slog.Error("Failed to remove player from game",
								"error", err,
								"roomID", roomID,
								"gameID", room.CurrentGame.state.ID)
						}
					}
					
					room.LastActivity = time.Now()
					roomToUpdate = room
					
					// Clean up empty rooms (no connected players)
					connectedCount := 0
					for _, player := range room.Players {
						if player.IsConnected {
							connectedCount++
						}
					}
					if connectedCount == 0 {
						delete(h.rooms, roomID)
						slog.Info("Removed empty room", "roomID", roomID)
						roomToUpdate = nil // Don't broadcast for empty rooms
					}
				}
				delete(h.clientToRoom, client)
			}

			delete(h.clients, client)
			close(client.Send)

			slog.Info("Golf client disconnected",
				"clientAddr", getClientAddr(client))
		}
	}()

	// Broadcast updated room state after releasing the lock
	if roomToUpdate != nil {
		h.broadcastRoomState(roomToUpdate)
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
		h.handleCreateRoom(msgData.Sender)
	case "joinGame":
		h.handleJoinRoom(msgData.Sender, msg.RoomID)
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
	if _, exists := h.clientToRoom[client]; exists {
		h.sendError(client, "Already in a room")
		return
	}

	// Create new room
	room := h.createRoom(client)
	h.rooms[room.ID] = room
	h.clientToRoom[client] = room.ID

	// Send room joined message
	player := room.Players[0] // First player is the creator
	h.sendRoomJoined(client, player.ID, room)

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

		// Check if client is already in a room
		if _, exists := h.clientToRoom[client]; exists {
			h.sendError(client, "Already in a room")
			return
		}

		// Add player to room
		var err error
		player, err = h.addPlayerToRoom(roomID, client)
		if err != nil {
			h.sendError(client, err.Error())
			return
		}

		room = h.rooms[roomID]
		h.clientToRoom[client] = roomID
	}()

	// Exit if join failed
	if room == nil || player == nil {
		return
	}

	// Send room joined message to new player
	h.sendRoomJoined(client, player.ID, room)

	// Broadcast updated state to all players in room
	h.broadcastRoomState(room)

	slog.Info("Player joined room",
		"roomID", roomID,
		"playerID", player.ID,
		"clientAddr", getClientAddr(client))
}

// handleStartGame starts a game within the current room
func (h *GolfHub) handleStartGame(client *hub.Client) {
	h.mu.Lock()
	room := h.getClientRoom(client)
	if room == nil {
		h.mu.Unlock()
		h.sendError(client, "Not in a room")
		return
	}
	
	// If there's no current game, create one
	if room.CurrentGame == nil {
		_, err := h.startNewGameInRoom(room.ID)
		if err != nil {
			h.mu.Unlock()
			h.sendError(client, err.Error())
			return
		}
		room = h.rooms[room.ID] // Refresh room reference
	}
	
	game := room.CurrentGame
	h.mu.Unlock()

	if err := game.StartGame(); err != nil {
		h.sendError(client, err.Error())
		return
	}

	// Broadcast game started message
	h.broadcastToGameLocked(game, &GameStartedMessage{Type: "gameStarted"})

	// Broadcast updated game state
	h.broadcastGameState(game)

	slog.Info("Game started", "gameID", game.state.ID, "roomID", room.ID)
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
	room := h.getClientRoom(client)
	if room == nil {
		return nil
	}
	return room.CurrentGame
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
	for client, roomID := range h.clientToRoom {
		if room, exists := h.rooms[roomID]; exists && room.CurrentGame != nil && room.CurrentGame.state.ID == game.state.ID {
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

// handleGameEnded processes game completion and updates room statistics
func (h *GolfHub) handleGameEnded(game *Game) {
	// First broadcast the game ended message
	h.broadcastGameEnded(game)
	
	// If this game belongs to a room, update room statistics
	roomID := game.GetRoomID()
	if roomID != "" {
		h.mu.Lock()
		err := h.completeGameInRoom(roomID)
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
	for client, roomID := range h.clientToRoom {
		if room, exists := h.rooms[roomID]; exists && room.CurrentGame != nil && room.CurrentGame.state.ID == game.state.ID {
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

// Room Management Methods

// createRoom creates a new room with the given client as the first player
func (h *GolfHub) createRoom(client *hub.Client) *Room {
	roomID := GenerateRoomID()
	clientID := getClientID(client)
	idGenerator := &players.WhimsicalIDGenerator{}
	playerName := idGenerator.GenerateID()
	
	player := &Player{
		ID:          fmt.Sprintf("player_%s_%d", roomID, 1),
		Name:        playerName,
		ClientID:    clientID,
		Cards:       CreateHiddenCards(),
		Score:       0,
		RevealedCards: make([]int, 0),
		IsReady:     false,
		HasPeeked:   false,
		TotalScore:  0,
		GamesPlayed: 0,
		GamesWon:    0,
		IsConnected: true,
		JoinedAt:    time.Now(),
	}
	
	room := &Room{
		ID:           roomID,
		Players:      []*Player{player},
		CurrentGame:  nil,
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
	
	clientID := getClientID(client)
	
	// Check if player is already in room
	for _, player := range room.Players {
		if player.ClientID == clientID {
			player.IsConnected = true
			room.LastActivity = time.Now()
			return player, nil
		}
	}
	
	// Create new player
	idGenerator := &players.WhimsicalIDGenerator{}
	playerName := idGenerator.GenerateID()
	playerNum := len(room.Players) + 1
	
	player := &Player{
		ID:          fmt.Sprintf("player_%s_%d", roomID, playerNum),
		Name:        playerName,
		ClientID:    clientID,
		Cards:       CreateHiddenCards(),
		Score:       0,
		RevealedCards: make([]int, 0),
		IsReady:     false,
		HasPeeked:   false,
		TotalScore:  0,
		GamesPlayed: 0,
		GamesWon:    0,
		IsConnected: true,
		JoinedAt:    time.Now(),
	}
	
	room.Players = append(room.Players, player)
	room.LastActivity = time.Now()
	
	return player, nil
}

// startNewGameInRoom starts a new game within a room
func (h *GolfHub) startNewGameInRoom(roomID string) (*Game, error) {
	room, exists := h.rooms[roomID]
	if !exists {
		return nil, fmt.Errorf("room not found")
	}
	
	if room.CurrentGame != nil && room.CurrentGame.state.GamePhase != "ended" {
		return nil, fmt.Errorf("game already in progress")
	}
	
	// Get connected players
	var connectedPlayers []*Player
	for _, player := range room.Players {
		if player.IsConnected {
			connectedPlayers = append(connectedPlayers, player)
		}
	}
	
	if len(connectedPlayers) < 2 {
		return nil, fmt.Errorf("need at least 2 connected players to start game")
	}
	
	if len(connectedPlayers) > 4 {
		return nil, fmt.Errorf("too many players for golf game")
	}
	
	// Create new game
	gameID := GenerateGameID()
	idGenerator := &players.WhimsicalIDGenerator{}
	game := NewGameInRoom(gameID, roomID, connectedPlayers, idGenerator)
	
	room.CurrentGame = game
	room.LastActivity = time.Now()
	
	return game, nil
}

// completeGameInRoom handles game completion and updates room stats
func (h *GolfHub) completeGameInRoom(roomID string) error {
	room, exists := h.rooms[roomID]
	if !exists {
		return fmt.Errorf("room not found")
	}
	
	if room.CurrentGame == nil || room.CurrentGame.state.GamePhase != "ended" {
		return fmt.Errorf("no completed game to process")
	}
	
	game := room.CurrentGame
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
	
	// Clear current game
	room.CurrentGame = nil
	
	return nil
}

// getClientRoom returns the room for a given client
func (h *GolfHub) getClientRoom(client *hub.Client) *Room {
	roomID, exists := h.clientToRoom[client]
	if !exists {
		return nil
	}
	return h.rooms[roomID]
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
	for client, roomID := range h.clientToRoom {
		if roomID == room.ID {
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
	
	// Start new game in room
	game, err := h.startNewGameInRoom(room.ID)
	if err != nil {
		h.mu.Unlock()
		h.sendError(client, err.Error())
		return
	}
	h.mu.Unlock()
	
	// Broadcast new game started message
	h.broadcastToRoom(room, &NewGameStartedMessage{Type: "newGameStarted"})
	
	// Broadcast updated room state
	h.broadcastRoomState(room)
	
	slog.Info("New game started in room", 
		"roomID", room.ID,
		"gameID", game.state.ID)
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
func (h *GolfHub) sendRoomJoined(client *hub.Client, playerID string, room *Room) {
	msg := &RoomJoinedMessage{
		Type:      "roomJoined",
		PlayerID:  playerID,
		RoomState: room,
	}
	h.sendJSON(client, msg)
}

// broadcastToRoom broadcasts a message to all clients in a room
func (h *GolfHub) broadcastToRoom(room *Room, message interface{}) {
	h.mu.RLock()
	defer h.mu.RUnlock()

	for client, roomID := range h.clientToRoom {
		if roomID == room.ID {
			h.sendJSON(client, message)
		}
	}
}
