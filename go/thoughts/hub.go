package main

import (
	"log"
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
}

func newHub() *Hub {
	return &Hub{
		gameMessage:    make(chan GameMessageData),
		register:       make(chan *Client),
		unregister:     make(chan *Client),
		clients:        make(map[*Client]bool),
		players:        make(map[string]*Player),
		clientToPlayer: make(map[*Client]string),
	}
}

func (h *Hub) run() {
	for {
		select {
		case client := <-h.register:
			h.clients[client] = true
			
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
		log.Printf("Error creating leave message: %v", err)
		return
	}
	
	h.broadcastToAll(leaveMsg)
}

// handleGameMessage processes incoming game protocol messages
func (h *Hub) handleGameMessage(msgData GameMessageData) {
	gameMsg, err := ParseGameMessage(msgData.Message)
	if err != nil {
		log.Printf("Error parsing game message: %v", err)
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
		log.Printf("Unknown message type: %s", gameMsg.Type)
	}
}

// handlePlayerJoin processes player join messages
func (h *Hub) handlePlayerJoin(msg *GameMessage, sender *Client) {
	if msg.Position == nil || msg.Color == nil || msg.Shape == nil {
		log.Printf("Invalid player_join message: missing position, color, or shape")
		return
	}
	
	// Validate position, color, and shape
	if err := ValidatePosition(*msg.Position); err != nil {
		log.Printf("Invalid position in join: %v", err)
		return
	}
	if err := ValidateColor(*msg.Color); err != nil {
		log.Printf("Invalid color in join: %v", err)
		return
	}
	if err := ValidateShape(*msg.Shape); err != nil {
		log.Printf("Invalid shape in join: %v", err)
		return
	}
	
	// Create player
	player := &Player{
		ID:       msg.PlayerID,
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
			log.Printf("Error creating game state: %v", err)
		}
	}
	
	// Store player mapping
	h.players[msg.PlayerID] = player
	h.clientToPlayer[sender] = msg.PlayerID
	
	// Broadcast join to all other clients
	if joinMsg, err := CreatePlayerJoinMessage(player); err == nil {
		h.broadcastToOthers(joinMsg, sender)
	}
}

// handlePositionUpdate processes position update messages
func (h *Hub) handlePositionUpdate(msg *GameMessage, sender *Client) {
	if msg.Position == nil {
		log.Printf("Invalid position_update message: missing position")
		return
	}
	
	// Validate position
	if err := ValidatePosition(*msg.Position); err != nil {
		log.Printf("Invalid position in update: %v", err)
		return
	}
	
	// Update player position
	if player, exists := h.players[msg.PlayerID]; exists {
		player.Position = *msg.Position
		
		// Broadcast to all other clients (don't echo back to sender)
		if updateMsg, err := CreatePositionUpdateMessage(msg.PlayerID, *msg.Position); err == nil {
			h.broadcastToOthers(updateMsg, sender)
		}
	}
}

// handleShapeUpdate processes shape update messages
func (h *Hub) handleShapeUpdate(msg *GameMessage, sender *Client) {
	if msg.Shape == nil {
		log.Printf("Invalid shape_update message: missing shape")
		return
	}
	
	// Validate shape
	if err := ValidateShape(*msg.Shape); err != nil {
		log.Printf("Invalid shape in update: %v", err)
		return
	}
	
	// Update player shape
	if player, exists := h.players[msg.PlayerID]; exists {
		player.Shape = *msg.Shape
		
		// Broadcast to all other clients (don't echo back to sender)
		if updateMsg, err := CreateShapeUpdateMessage(msg.PlayerID, *msg.Shape); err == nil {
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
