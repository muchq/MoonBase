package thoughts

import (
	"encoding/json"
	"sync"
	"testing"
	"time"

	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/hub"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/players"
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

func (m *mockClient) clearMessages() {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.messages = nil
}

func (m *mockClient) close() {
	m.mu.Lock()
	m.closed = true
	m.mu.Unlock()
	close(m.send)
}

func TestHub_PlayerJoin(t *testing.T) {
	// Use deterministic ID generator for predictable tests
	thoughtsHub := NewThoughtsHubWithIDGenerator(players.NewDeterministicIDGenerator())
	go thoughtsHub.Run()
	defer func() {
		// Clean shutdown would require more complex coordination
	}()

	// Create mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	// Convert mock clients to actual Client structs
	hubClient1 := &hub.Client{Hub: thoughtsHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: thoughtsHub, Send: client2.send}

	// Register clients (they will receive welcome messages)
	thoughtsHub.Register(hubClient1)
	thoughtsHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond) // Allow processing

	// Check welcome messages were sent
	messages1 := client1.getMessages()
	if len(messages1) != 1 {
		t.Fatalf("Expected 1 welcome message for first player, got %d", len(messages1))
	}
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 welcome message for second player, got %d", len(messages2))
	}

	// Clear messages to test join flow
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 joins (no playerId in message - server assigns)
	joinMsg1 := `{
		"type": "player_join",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"shape": 0,
		"timestamp": 1703123456789
	}`

	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg1),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond) // Allow processing

	// Check that player 1 got no messages (no existing players to show)
	messages1 = client1.getMessages()
	if len(messages1) != 0 {
		t.Fatalf("Expected 0 messages for first player, got %d", len(messages1))
	}

	// Check that player 2 got the join broadcast
	messages2 = client2.getMessages()
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
	// Use deterministic ID generator for predictable tests
	thoughtsHub := NewThoughtsHubWithIDGenerator(players.NewDeterministicIDGenerator())
	go thoughtsHub.Run()

	// Create and register mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	hubClient1 := &hub.Client{Hub: thoughtsHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: thoughtsHub, Send: client2.send}

	thoughtsHub.Register(hubClient1)
	thoughtsHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)

	// Clear welcome messages
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 joins first (no playerId in client message)
	joinMsg := `{
		"type": "player_join",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"shape": 0,
		"timestamp": 1703123456789
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Clear messages to focus on position update
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 sends position update (no playerId in client message)
	updateMsg := `{
		"type": "position_update",
		"position": [15.0, 0, -8.0],
		"timestamp": 1703123456890
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(updateMsg),
		Sender:  hubClient1,
	})
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
	// Use deterministic ID generator for predictable tests
	thoughtsHub := &ThoughtsHub{
		gameMessage:    make(chan hub.GameMessageData),
		register:       make(chan *hub.Client),
		unregister:     make(chan *hub.Client),
		clients:        make(map[*hub.Client]bool),
		players:        make(map[string]*Player),
		clientToPlayer: make(map[*hub.Client]string),
		idGenerator:    players.NewDeterministicIDGenerator(),
	}
	go thoughtsHub.Run()

	// Create and register mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	hubClient1 := &hub.Client{Hub: thoughtsHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: thoughtsHub, Send: client2.send}

	thoughtsHub.Register(hubClient1)
	thoughtsHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)

	// Clear welcome messages
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 joins (no playerId in client message)
	joinMsg := `{
		"type": "player_join",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"shape": 0,
		"timestamp": 1703123456789
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify player is tracked
	if len(thoughtsHub.players) != 1 {
		t.Errorf("Expected 1 player in hub, got %d", len(thoughtsHub.players))
	}
	if len(thoughtsHub.clientToPlayer) != 2 {
		t.Errorf("Expected 2 client mappings, got %d", len(thoughtsHub.clientToPlayer))
	}

	// Clear messages to focus on leave handling
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 disconnects (unregister)
	thoughtsHub.Unregister(hubClient1)
	time.Sleep(10 * time.Millisecond)

	// Verify player is removed from tracking
	if len(thoughtsHub.players) != 0 {
		t.Errorf("Expected 0 players after disconnect, got %d", len(thoughtsHub.players))
	}
	if len(thoughtsHub.clientToPlayer) != 1 {
		t.Errorf("Expected 1 client mapping after disconnect, got %d", len(thoughtsHub.clientToPlayer))
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

func TestHub_ShapeUpdate(t *testing.T) {
	// Use deterministic ID generator for predictable tests
	thoughtsHub := &ThoughtsHub{
		gameMessage:    make(chan hub.GameMessageData),
		register:       make(chan *hub.Client),
		unregister:     make(chan *hub.Client),
		clients:        make(map[*hub.Client]bool),
		players:        make(map[string]*Player),
		clientToPlayer: make(map[*hub.Client]string),
		idGenerator:    players.NewDeterministicIDGenerator(),
	}
	go thoughtsHub.Run()

	// Create and register mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()

	hubClient1 := &hub.Client{Hub: thoughtsHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: thoughtsHub, Send: client2.send}

	thoughtsHub.Register(hubClient1)
	thoughtsHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)

	// Clear welcome messages
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 joins first (no playerId in client message)
	joinMsg := `{
		"type": "player_join",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"shape": 0,
		"timestamp": 1703123456789
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Clear messages to focus on shape update
	client1.clearMessages()
	client2.clearMessages()

	// Player 1 sends shape update (sphere to cube, no playerId in client message)
	shapeUpdateMsg := `{
		"type": "shape_update",
		"shape": 1,
		"timestamp": 1703123456950
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(shapeUpdateMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Player 1 should NOT receive the echo
	messages1 := client1.getMessages()
	if len(messages1) != 0 {
		t.Errorf("Player 1 should not receive echo of their own shape update, got %d messages", len(messages1))
	}

	// Player 2 should receive the shape update
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 shape update for player 2, got %d", len(messages2))
	}

	var shapeUpdate ShapeUpdateMessage
	if err := json.Unmarshal(messages2[0], &shapeUpdate); err != nil {
		t.Fatalf("Failed to parse shape update: %v", err)
	}
	if shapeUpdate.Type != "shape_update" {
		t.Errorf("Expected shape_update, got %s", shapeUpdate.Type)
	}
	if shapeUpdate.PlayerID != "player-1" {
		t.Errorf("Expected player-1, got %s", shapeUpdate.PlayerID)
	}
	expectedShape := Shape(1)
	if shapeUpdate.Shape != expectedShape {
		t.Errorf("Expected shape %v, got %v", expectedShape, shapeUpdate.Shape)
	}

	// Verify the player's shape was updated in the hub
	if player, exists := thoughtsHub.players["player-1"]; exists {
		if player.Shape != Shape(1) {
			t.Errorf("Expected player shape to be updated to 1, got %v", player.Shape)
		}
	} else {
		t.Error("Player-1 not found in hub after shape update")
	}

	client1.close()
	client2.close()
}

func TestHub_InvalidMessages(t *testing.T) {
	// Use deterministic ID generator for predictable tests
	thoughtsHub := NewThoughtsHubWithIDGenerator(players.NewDeterministicIDGenerator())
	go thoughtsHub.Run()

	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: thoughtsHub, Send: client1.send}

	thoughtsHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)

	// Clear welcome message
	client1.clearMessages()

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
			msg:  `{"type":"player_join","color":[0.8,0.2,0.6],"timestamp":123}`,
		},
		{
			name: "missing color in join",
			msg:  `{"type":"player_join","position":[10,0,-5],"shape":0,"timestamp":123}`,
		},
		{
			name: "missing shape in join",
			msg:  `{"type":"player_join","position":[10,0,-5],"color":[0.8,0.2,0.6],"timestamp":123}`,
		},
		{
			name: "invalid position boundary",
			msg:  `{"type":"player_join","position":[100,0,-5],"color":[0.8,0.2,0.6],"shape":0,"timestamp":123}`,
		},
		{
			name: "invalid color range",
			msg:  `{"type":"player_join","position":[10,0,-5],"color":[1.5,0.2,0.6],"shape":0,"timestamp":123}`,
		},
		{
			name: "invalid shape range negative",
			msg:  `{"type":"player_join","position":[10,0,-5],"color":[0.8,0.2,0.6],"shape":-1,"timestamp":123}`,
		},
		{
			name: "invalid shape range too high",
			msg:  `{"type":"player_join","position":[10,0,-5],"color":[0.8,0.2,0.6],"shape":3,"timestamp":123}`,
		},
		{
			name: "unknown message type",
			msg:  `{"type":"unknown_type","timestamp":123}`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Clear previous messages
			client1.clearMessages()

			// Send invalid message
			thoughtsHub.GameMessage(hub.GameMessageData{
				Message: []byte(tt.msg),
				Sender:  hubClient1,
			})
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
	// Use deterministic ID generator for predictable tests
	thoughtsHub := NewThoughtsHubWithIDGenerator(players.NewDeterministicIDGenerator())
	go thoughtsHub.Run()

	// Create first player
	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: thoughtsHub, Send: client1.send}
	thoughtsHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)

	// Clear welcome message
	client1.clearMessages()

	// Player 1 joins (no playerId in client message)
	joinMsg1 := `{
		"type": "player_join",
		"position": [10.0, 0, -5.0],
		"color": [0.8, 0.2, 0.6],
		"shape": 0,
		"timestamp": 1703123456789
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg1),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Create second player
	client2 := newMockClient("client2")
	client2.collectMessages()
	hubClient2 := &hub.Client{Hub: thoughtsHub, Send: client2.send}
	thoughtsHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)

	// Clear welcome message for player 2
	client2.clearMessages()

	// Player 2 joins (no playerId in client message)
	joinMsg2 := `{
		"type": "player_join",
		"position": [20.0, 0, 15.0],
		"color": [0.3, 0.9, 0.4],
		"shape": 1,
		"timestamp": 1703123457000
	}`
	thoughtsHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg2),
		Sender:  hubClient2,
	})
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
