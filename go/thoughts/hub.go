package main

import (
	"log/slog"
)

// GameMessageData represents a message with its sender
type GameMessageData struct {
	Message []byte
	Sender  *Client
}

// Hub maintains the set of active clients and broadcasts messages to the
// clients.
type Hub struct {
	// Registered clients.
	clients map[*Client]bool

	// Active players mapped by their game ID
	players map[string]*Player

	// Client to player ID mapping
	clientToPlayer map[*Client]string

	// Inbound game messages from the clients.
	gameMessage chan GameMessageData

	// Register requests from the clients.
	register chan *Client

	// Unregister requests from clients.
	unregister chan *Client

	// Player ID generator
	idGenerator PlayerIDGenerator
}

func newHub() *Hub {
	return newHubWithIDGenerator(&RandomIDGenerator{})
}

func newHubWithIDGenerator(idGenerator PlayerIDGenerator) *Hub {
	return &Hub{
		gameMessage:    make(chan GameMessageData),
		register:       make(chan *Client),
		unregister:     make(chan *Client),
		clients:        make(map[*Client]bool),
		players:        make(map[string]*Player),
		clientToPlayer: make(map[*Client]string),
		idGenerator:    idGenerator,
	}
}

func (h *Hub) run() {
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
				if client.conn != nil {
					clientAddr = client.conn.RemoteAddr().String()
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
				close(client.send)
			}
			
		case msgData := <-h.gameMessage:
			h.handleGameMessage(msgData)
		}
	}
}

// handlePlayerLeave processes a player disconnect
func (h *Hub) handlePlayerLeave(playerID string, client *Client) {
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
func (h *Hub) handleGameMessage(msgData GameMessageData) {
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
func (h *Hub) handlePlayerJoin(msg *GameMessage, sender *Client) {
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
func (h *Hub) handlePositionUpdate(msg *GameMessage, sender *Client) {
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
func (h *Hub) handleShapeUpdate(msg *GameMessage, sender *Client) {
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
func (h *Hub) handlePlayerLeaveMessage(msg *GameMessage, sender *Client) {
	if playerID, exists := h.clientToPlayer[sender]; exists {
		h.handlePlayerLeave(playerID, sender)
	}
}

// broadcastToAll sends a message to all connected clients
func (h *Hub) broadcastToAll(message []byte) {
	for client := range h.clients {
		h.sendToClient(client, message)
	}
}

// broadcastToOthers sends a message to all clients except the sender
func (h *Hub) broadcastToOthers(message []byte, sender *Client) {
	for client := range h.clients {
		if client != sender {
			h.sendToClient(client, message)
		}
	}
}

// sendToClient sends a message to a specific client
func (h *Hub) sendToClient(client *Client, message []byte) {
	select {
	case client.send <- message:
	default:
		close(client.send)
		delete(h.clients, client)
		if playerID, exists := h.clientToPlayer[client]; exists {
			delete(h.players, playerID)
			delete(h.clientToPlayer, client)
		}
	}
}
