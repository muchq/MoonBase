package main

import (
	"encoding/json"
	"sync"
	"testing"
	"time"
)

// mockClient simulates a websocket client for testing
type mockClient struct {
	id       string
	messages [][]byte
	send     chan []byte
	closed   bool
	mu       sync.Mutex
}

func newMockClient(id string) *mockClient {
	return &mockClient{
		id:       id,
		messages: make([][]byte, 0),
		send:     make(chan []byte, 256),
	}
}

func (m *mockClient) collectMessages() {
	go func() {
		for msg := range m.send {
			m.mu.Lock()
			if !m.closed {
				m.messages = append(m.messages, msg)
			}
			m.mu.Unlock()
		}
	}()
}

func (m *mockClient) getMessages() [][]byte {
	m.mu.Lock()
	defer m.mu.Unlock()
	result := make([][]byte, len(m.messages))
	copy(result, m.messages)
	return result
}

func (m *mockClient) close() {
	m.mu.Lock()
	m.closed = true
	m.mu.Unlock()
	close(m.send)
}

func TestHub_PlayerJoin(t *testing.T) {
	hub := newHub()
	go hub.run()
	defer func() {
		// Clean shutdown would require more complex coordination
	}()

	// Create mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	// Convert mock clients to actual Client structs
	hubClient1 := &Client{hub: hub, send: client1.send}
	hubClient2 := &Client{hub: hub, send: client2.send}

	// Register clients
	hub.register <- hubClient1
	hub.register <- hubClient2
	time.Sleep(10 * time.Millisecond) // Allow processing

	// Player 1 joins
	joinMsg1 := `{
		"type": "player_join",
		"playerId": "player-1",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"timestamp": 1703123456789
	}`

	hub.gameMessage <- GameMessageData{
		Message: []byte(joinMsg1),
		Sender:  hubClient1,
	}
	time.Sleep(10 * time.Millisecond) // Allow processing

	// Check that player 1 got no messages (no existing players to show)
	messages1 := client1.getMessages()
	if len(messages1) != 0 {
		t.Fatalf("Expected 0 messages for first player, got %d", len(messages1))
	}

	// Check that player 2 got the join broadcast
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 message for player 2, got %d", len(messages2))
	}

	var joinBroadcast PlayerJoinMessage
	if err := json.Unmarshal(messages2[0], &joinBroadcast); err != nil {
		t.Fatalf("Failed to parse join broadcast: %v", err)
	}
	if joinBroadcast.Type != "player_join" {
		t.Errorf("Expected player_join message, got %s", joinBroadcast.Type)
	}
	if joinBroadcast.PlayerID != "player-1" {
		t.Errorf("Expected player-1, got %s", joinBroadcast.PlayerID)
	}

	client1.close()
	client2.close()
}

func TestHub_PositionUpdate(t *testing.T) {
	hub := newHub()
	go hub.run()

	// Create and register mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	hubClient1 := &Client{hub: hub, send: client1.send}
	hubClient2 := &Client{hub: hub, send: client2.send}

	hub.register <- hubClient1
	hub.register <- hubClient2
	time.Sleep(10 * time.Millisecond)

	// Player 1 joins first
	joinMsg := `{
		"type": "player_join",
		"playerId": "player-1",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"timestamp": 1703123456789
	}`
	hub.gameMessage <- GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient1,
	}
	time.Sleep(10 * time.Millisecond)

	// Clear messages to focus on position update
	client1.messages = nil
	client2.messages = nil

	// Player 1 sends position update
	updateMsg := `{
		"type": "position_update",
		"playerId": "player-1",
		"position": [15.0, 0, -8.0],
		"timestamp": 1703123456890
	}`
	hub.gameMessage <- GameMessageData{
		Message: []byte(updateMsg),
		Sender:  hubClient1,
	}
	time.Sleep(10 * time.Millisecond)

	// Player 1 should NOT receive the echo
	messages1 := client1.getMessages()
	if len(messages1) != 0 {
		t.Errorf("Player 1 should not receive echo of their own position update, got %d messages", len(messages1))
	}

	// Player 2 should receive the position update
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 position update for player 2, got %d", len(messages2))
	}

	var posUpdate PositionUpdateMessage
	if err := json.Unmarshal(messages2[0], &posUpdate); err != nil {
		t.Fatalf("Failed to parse position update: %v", err)
	}
	if posUpdate.Type != "position_update" {
		t.Errorf("Expected position_update, got %s", posUpdate.Type)
	}
	if posUpdate.PlayerID != "player-1" {
		t.Errorf("Expected player-1, got %s", posUpdate.PlayerID)
	}
	expectedPos := Position{15.0, 0, -8.0}
	if posUpdate.Position != expectedPos {
		t.Errorf("Expected position %v, got %v", expectedPos, posUpdate.Position)
	}

	client1.close()
	client2.close()
}

func TestHub_PlayerLeave(t *testing.T) {
	hub := newHub()
	go hub.run()

	// Create and register mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	hubClient1 := &Client{hub: hub, send: client1.send}
	hubClient2 := &Client{hub: hub, send: client2.send}

	hub.register <- hubClient1
	hub.register <- hubClient2
	time.Sleep(10 * time.Millisecond)

	// Player 1 joins
	joinMsg := `{
		"type": "player_join",
		"playerId": "player-1",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"timestamp": 1703123456789
	}`
	hub.gameMessage <- GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient1,
	}
	time.Sleep(10 * time.Millisecond)

	// Verify player is tracked
	if len(hub.players) != 1 {
		t.Errorf("Expected 1 player in hub, got %d", len(hub.players))
	}
	if len(hub.clientToPlayer) != 1 {
		t.Errorf("Expected 1 client mapping, got %d", len(hub.clientToPlayer))
	}

	// Clear messages to focus on leave handling
	client1.messages = nil
	client2.messages = nil

	// Player 1 disconnects (unregister)
	hub.unregister <- hubClient1
	time.Sleep(10 * time.Millisecond)

	// Verify player is removed from tracking
	if len(hub.players) != 0 {
		t.Errorf("Expected 0 players after disconnect, got %d", len(hub.players))
	}
	if len(hub.clientToPlayer) != 0 {
		t.Errorf("Expected 0 client mappings after disconnect, got %d", len(hub.clientToPlayer))
	}

	// Player 2 should receive leave broadcast
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 leave message for player 2, got %d", len(messages2))
	}

	var leaveMsg PlayerLeaveMessage
	if err := json.Unmarshal(messages2[0], &leaveMsg); err != nil {
		t.Fatalf("Failed to parse leave message: %v", err)
	}
	if leaveMsg.Type != "player_leave" {
		t.Errorf("Expected player_leave, got %s", leaveMsg.Type)
	}
	if leaveMsg.PlayerID != "player-1" {
		t.Errorf("Expected player-1, got %s", leaveMsg.PlayerID)
	}

	client2.close()
}

func TestHub_InvalidMessages(t *testing.T) {
	hub := newHub()
	go hub.run()

	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &Client{hub: hub, send: client1.send}

	hub.register <- hubClient1
	time.Sleep(10 * time.Millisecond)

	tests := []struct {
		name string
		msg  string
	}{
		{
			name: "invalid JSON",
			msg:  `{"type":"player_join","playerId":}`,
		},
		{
			name: "missing position in join",
			msg:  `{"type":"player_join","playerId":"player-1","color":[0.8,0.2,0.6],"timestamp":123}`,
		},
		{
			name: "missing color in join",
			msg:  `{"type":"player_join","playerId":"player-1","position":[10,0,-5],"timestamp":123}`,
		},
		{
			name: "invalid position boundary",
			msg:  `{"type":"player_join","playerId":"player-1","position":[100,0,-5],"color":[0.8,0.2,0.6],"timestamp":123}`,
		},
		{
			name: "invalid color range",
			msg:  `{"type":"player_join","playerId":"player-1","position":[10,0,-5],"color":[1.5,0.2,0.6],"timestamp":123}`,
		},
		{
			name: "unknown message type",
			msg:  `{"type":"unknown_type","playerId":"player-1","timestamp":123}`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Clear previous messages
			client1.messages = nil

			// Send invalid message
			hub.gameMessage <- GameMessageData{
				Message: []byte(tt.msg),
				Sender:  hubClient1,
			}
			time.Sleep(10 * time.Millisecond)

			// Should not crash and should not send any messages back
			messages := client1.getMessages()
			if len(messages) > 0 {
				t.Errorf("Expected no messages for invalid input, got %d", len(messages))
			}
		})
	}

	client1.close()
}

func TestHub_GameStateForNewPlayer(t *testing.T) {
	hub := newHub()
	go hub.run()

	// Create first player
	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &Client{hub: hub, send: client1.send}
	hub.register <- hubClient1
	time.Sleep(10 * time.Millisecond)

	// Player 1 joins
	joinMsg1 := `{
		"type": "player_join",
		"playerId": "player-1",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"timestamp": 1703123456789
	}`
	hub.gameMessage <- GameMessageData{
		Message: []byte(joinMsg1),
		Sender:  hubClient1,
	}
	time.Sleep(10 * time.Millisecond)

	// Create second player
	client2 := newMockClient("client2")
	client2.collectMessages()
	hubClient2 := &Client{hub: hub, send: client2.send}
	hub.register <- hubClient2
	time.Sleep(10 * time.Millisecond)

	// Player 2 joins
	joinMsg2 := `{
		"type": "player_join",
		"playerId": "player-2",
		"position": [20.0, 0, 15.0],
		"color": [0.3, 0.9, 0.4],
		"timestamp": 1703123457000
	}`
	hub.gameMessage <- GameMessageData{
		Message: []byte(joinMsg2),
		Sender:  hubClient2,
	}
	time.Sleep(10 * time.Millisecond)

	// Player 2 should receive game state with just player 1 (existing players)
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 game state message for player 2, got %d", len(messages2))
	}

	var gameState GameStateMessage
	if err := json.Unmarshal(messages2[0], &gameState); err != nil {
		t.Fatalf("Failed to parse game state: %v", err)
	}

	if gameState.Type != "game_state" {
		t.Errorf("Expected game_state, got %s", gameState.Type)
	}
	if len(gameState.Players) != 1 {
		t.Errorf("Expected 1 existing player in game state, got %d", len(gameState.Players))
	}

	// Verify player 1 is present (the existing player)
	if gameState.Players[0].PlayerID != "player-1" {
		t.Errorf("Expected existing player-1 in game state, got %s", gameState.Players[0].PlayerID)
	}

	client1.close()
	client2.close()
}