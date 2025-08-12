package thoughts

import (
	"log/slog"

	"github.com/muchq/moonbase/go/games_ws_backend/hub"
	"github.com/muchq/moonbase/go/games_ws_backend/players"
)

// GameMessageData represents a message with its sender
type GameMessageData struct {
	Message []byte
	Sender  *hub.Client
}

// ThoughtsHub maintains the set of active clients and broadcasts messages to the
// clients.
type ThoughtsHub struct {
	// Registered clients.
	clients map[*hub.Client]bool

	// Active players mapped by their game ID
	players map[string]*Player

	// Client to player ID mapping
	clientToPlayer map[*hub.Client]string

	// Inbound game messages from the clients.
	gameMessage chan hub.GameMessageData

	// Register requests from the clients.
	register chan *hub.Client

	// Unregister requests from clients.
	unregister chan *hub.Client

	// Player ID generator
	idGenerator players.PlayerIDGenerator
}

func (t *ThoughtsHub) Register(c *hub.Client) {
	t.register <- c
}

func (t *ThoughtsHub) Unregister(c *hub.Client) {
	t.unregister <- c
}

func (t *ThoughtsHub) GameMessage(data hub.GameMessageData) {
	t.gameMessage <- data
}

func NewThoughtsHub() hub.Hub {
	return NewThoughtsHubWithIDGenerator(&players.WhimsicalIDGenerator{})
}

func NewThoughtsHubWithIDGenerator(idGenerator players.PlayerIDGenerator) hub.Hub {
	return &ThoughtsHub{
		gameMessage:    make(chan hub.GameMessageData),
		register:       make(chan *hub.Client),
		unregister:     make(chan *hub.Client),
		clients:        make(map[*hub.Client]bool),
		players:        make(map[string]*Player),
		clientToPlayer: make(map[*hub.Client]string),
		idGenerator:    idGenerator,
	}
}

func (h *ThoughtsHub) Run() {
	for {
		select {
		case client := <-h.register:
			h.clients[client] = true

			// Generate player ID and send welcome message immediately
			playerID := h.idGenerator.GenerateID()
			h.clientToPlayer[client] = playerID

			if welcomeMsg, err := CreateWelcomeMessage(playerID); err == nil {
				h.sendToClient(client, welcomeMsg)

				// Get client address safely (may be nil in tests)
				var clientAddr string
				if client.Conn != nil {
					clientAddr = client.Conn.RemoteAddr().String()
				} else {
					clientAddr = "test-client"
				}

				slog.Info("Client registered and welcomed",
					"playerID", playerID,
					"clientAddr", clientAddr)
			} else {
				slog.Error("Failed to create welcome message",
					"error", err,
					"playerID", playerID)
			}

		case client := <-h.unregister:
			if _, ok := h.clients[client]; ok {
				// Handle player leaving
				if playerID, exists := h.clientToPlayer[client]; exists {
					h.handlePlayerLeave(playerID, client)
				}
				delete(h.clients, client)
				close(client.Send)
			}

		case msgData := <-h.gameMessage:
			h.handleGameMessage(msgData)
		}
	}
}

// handlePlayerLeave processes a player disconnect
func (h *ThoughtsHub) handlePlayerLeave(playerID string, client *hub.Client) {
	// Remove player from tracking
	delete(h.players, playerID)
	delete(h.clientToPlayer, client)

	// Broadcast leave message to all other clients
	leaveMsg, err := CreatePlayerLeaveMessage(playerID)
	if err != nil {
		slog.Error("Failed to create leave message",
			"error", err,
			"playerID", playerID)
		return
	}

	h.broadcastToAll(leaveMsg)
	slog.Info("Player left",
		"playerID", playerID,
		"remainingPlayers", len(h.players))
}

// handleGameMessage processes incoming game protocol messages
func (h *ThoughtsHub) handleGameMessage(msgData hub.GameMessageData) {
	gameMsg, err := ParseGameMessage(msgData.Message)
	if err != nil {
		slog.Error("Failed to parse game message",
			"error", err,
			"messageLength", len(msgData.Message))
		return
	}

	switch gameMsg.Type {
	case "player_join":
		h.handlePlayerJoin(gameMsg, msgData.Sender)
	case "position_update":
		h.handlePositionUpdate(gameMsg, msgData.Sender)
	case "shape_update":
		h.handleShapeUpdate(gameMsg, msgData.Sender)
	case "player_leave":
		h.handlePlayerLeaveMessage(gameMsg, msgData.Sender)
	default:
		slog.Warn("Unknown message type",
			"type", gameMsg.Type,
			"playerID", gameMsg.PlayerID)
	}
}

// handlePlayerJoin processes player join messages
func (h *ThoughtsHub) handlePlayerJoin(msg *GameMessage, sender *hub.Client) {
	// Get the server-assigned player ID for this client
	playerID, exists := h.clientToPlayer[sender]
	if !exists {
		slog.Error("Player join from unregistered client")
		return
	}

	if msg.Position == nil || msg.Color == nil || msg.Shape == nil {
		slog.Warn("Invalid player_join message: missing required fields",
			"playerID", playerID,
			"hasPosition", msg.Position != nil,
			"hasColor", msg.Color != nil,
			"hasShape", msg.Shape != nil)
		return
	}

	// Validate position, color, and shape
	if err := ValidatePosition(*msg.Position); err != nil {
		slog.Warn("Invalid position in join",
			"error", err,
			"playerID", playerID,
			"position", *msg.Position)
		return
	}
	if err := ValidateColor(*msg.Color); err != nil {
		slog.Warn("Invalid color in join",
			"error", err,
			"playerID", playerID,
			"color", *msg.Color)
		return
	}
	if err := ValidateShape(*msg.Shape); err != nil {
		slog.Warn("Invalid shape in join",
			"error", err,
			"playerID", playerID,
			"shape", *msg.Shape)
		return
	}

	// Create player using server-assigned ID
	player := &Player{
		ID:       playerID,
		Position: *msg.Position,
		Color:    *msg.Color,
		Shape:    *msg.Shape,
		Client:   sender,
	}

	// Send current game state to new player (before adding them)
	if len(h.players) > 0 {
		if gameState, err := CreateGameStateMessage(h.players); err == nil {
			h.sendToClient(sender, gameState)
		} else {
			slog.Error("Failed to create game state",
				"error", err,
				"playerCount", len(h.players))
		}
	}

	// Store player mapping (clientToPlayer already set during registration)
	h.players[playerID] = player

	// Broadcast join to all other clients
	if joinMsg, err := CreatePlayerJoinMessage(player); err == nil {
		h.broadcastToOthers(joinMsg, sender)
		slog.Info("Player joined",
			"playerID", playerID,
			"position", *msg.Position,
			"shape", *msg.Shape,
			"totalPlayers", len(h.players))
	}
}

// handlePositionUpdate processes position update messages
func (h *ThoughtsHub) handlePositionUpdate(msg *GameMessage, sender *hub.Client) {
	// Get the server-assigned player ID for this client
	playerID, exists := h.clientToPlayer[sender]
	if !exists {
		slog.Error("Position update from unregistered client")
		return
	}

	if msg.Position == nil {
		slog.Warn("Invalid position_update message: missing position",
			"playerID", playerID)
		return
	}

	// Validate position
	if err := ValidatePosition(*msg.Position); err != nil {
		slog.Warn("Invalid position in update",
			"error", err,
			"playerID", playerID,
			"position", *msg.Position)
		return
	}

	// Update player position
	if player, exists := h.players[playerID]; exists {
		player.Position = *msg.Position

		// Broadcast to all other clients (don't echo back to sender)
		if updateMsg, err := CreatePositionUpdateMessage(playerID, *msg.Position); err == nil {
			h.broadcastToOthers(updateMsg, sender)
		}
	}
}

// handleShapeUpdate processes shape update messages
func (h *ThoughtsHub) handleShapeUpdate(msg *GameMessage, sender *hub.Client) {
	// Get the server-assigned player ID for this client
	playerID, exists := h.clientToPlayer[sender]
	if !exists {
		slog.Error("Shape update from unregistered client")
		return
	}

	if msg.Shape == nil {
		slog.Warn("Invalid shape_update message: missing shape",
			"playerID", playerID)
		return
	}

	// Validate shape
	if err := ValidateShape(*msg.Shape); err != nil {
		slog.Warn("Invalid shape in update",
			"error", err,
			"playerID", playerID,
			"shape", *msg.Shape)
		return
	}

	// Update player shape
	if player, exists := h.players[playerID]; exists {
		player.Shape = *msg.Shape

		// Broadcast to all other clients (don't echo back to sender)
		if updateMsg, err := CreateShapeUpdateMessage(playerID, *msg.Shape); err == nil {
			h.broadcastToOthers(updateMsg, sender)
		}
	}
}

// handlePlayerLeaveMessage processes explicit player leave messages
func (h *ThoughtsHub) handlePlayerLeaveMessage(msg *GameMessage, sender *hub.Client) {
	if playerID, exists := h.clientToPlayer[sender]; exists {
		h.handlePlayerLeave(playerID, sender)
	}
}

// broadcastToAll sends a message to all connected clients
func (h *ThoughtsHub) broadcastToAll(message []byte) {
	for client := range h.clients {
		h.sendToClient(client, message)
	}
}

// broadcastToOthers sends a message to all clients except the sender
func (h *ThoughtsHub) broadcastToOthers(message []byte, sender *hub.Client) {
	for client := range h.clients {
		if client != sender {
			h.sendToClient(client, message)
		}
	}
}

// sendToClient sends a message to a specific client
func (h *ThoughtsHub) sendToClient(client *hub.Client, message []byte) {
	select {
	case client.Send <- message:
	default:
		close(client.Send)
		delete(h.clients, client)
		if playerID, exists := h.clientToPlayer[client]; exists {
			delete(h.players, playerID)
			delete(h.clientToPlayer, client)
		}
	}
}
