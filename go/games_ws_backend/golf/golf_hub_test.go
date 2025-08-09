package golf

import (
	"encoding/json"
	"sync"
	"testing"
	"time"

	"github.com/muchq/moonbase/go/games_ws_backend/hub"
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

func TestParseIncomingMessage(t *testing.T) {
	tests := []struct {
		name    string
		json    string
		msgType string
		gameID  string
		index   int
	}{
		{
			name:    "create game",
			json:    `{"type": "createGame"}`,
			msgType: "createGame",
		},
		{
			name:    "join game",
			json:    `{"type": "joinGame", "gameId": "ABC123"}`,
			msgType: "joinGame",
			gameID:  "ABC123",
		},
		{
			name:    "peek card",
			json:    `{"type": "peekCard", "cardIndex": 3}`,
			msgType: "peekCard",
			index:   3,
		},
	}
	
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			msg, err := ParseIncomingMessage([]byte(test.json))
			if err != nil {
				t.Fatalf("Failed to parse message: %v", err)
			}
			
			if msg.Type != test.msgType {
				t.Errorf("Expected type %s, got %s", test.msgType, msg.Type)
			}
			
			if test.gameID != "" && msg.GameID != test.gameID {
				t.Errorf("Expected gameID %s, got %s", test.gameID, msg.GameID)
			}
			
			if test.msgType == "peekCard" && msg.CardIndex != test.index {
				t.Errorf("Expected cardIndex %d, got %d", test.index, msg.CardIndex)
			}
		})
	}
}

// Hub Integration Tests

func TestHub_CreateAndJoinGame(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()
	
	// Create mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	
	// Convert to hub clients
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	
	// Register clients
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)
	
	// Client 1 creates game
	createMsg := `{"type": "createGame"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Check client 1 received gameJoined message
	messages1 := client1.getMessages()
	if len(messages1) != 1 {
		t.Fatalf("Expected 1 message for client1, got %d", len(messages1))
	}
	
	var joinedMsg GameJoinedMessage
	if err := json.Unmarshal(messages1[0], &joinedMsg); err != nil {
		t.Fatalf("Failed to parse gameJoined message: %v", err)
	}
	
	if joinedMsg.Type != "gameJoined" {
		t.Errorf("Expected gameJoined message, got %s", joinedMsg.Type)
	}
	
	gameID := joinedMsg.GameState.ID
	if len(gameID) != 6 {
		t.Errorf("Expected 6-character game ID, got %s", gameID)
	}
	
	// Client 2 joins the game
	client2.clearMessages()
	joinMsg := `{"type": "joinGame", "gameId": "` + gameID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Check client 2 received gameJoined message (and possibly a game state broadcast)
	messages2 := client2.getMessages()
	if len(messages2) == 0 {
		t.Fatal("Expected at least 1 message for client2")
	}
	
	var joined2Msg GameJoinedMessage
	if err := json.Unmarshal(messages2[0], &joined2Msg); err != nil {
		t.Fatalf("Failed to parse gameJoined message: %v", err)
	}
	
	if joined2Msg.GameState.ID != gameID {
		t.Errorf("Expected to join game %s, got %s", gameID, joined2Msg.GameState.ID)
	}
	
	if len(joined2Msg.GameState.Players) != 2 {
		t.Errorf("Expected 2 players in game, got %d", len(joined2Msg.GameState.Players))
	}
	
	client1.close()
	client2.close()
}

func TestHub_StartGame(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()
	
	// Create and register clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)
	
	// Create game and get both players in
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	messages1 := client1.getMessages()
	var joinedMsg GameJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	gameID := joinedMsg.GameState.ID
	
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Clear messages
	client1.clearMessages()
	client2.clearMessages()
	
	// Start game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "startGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Both clients should receive gameStarted message
	messages1 = client1.getMessages()
	messages2 := client2.getMessages()
	
	// Look for gameStarted message
	foundStarted1 := false
	foundStarted2 := false
	
	for _, msg := range messages1 {
		var parsed map[string]interface{}
		if err := json.Unmarshal(msg, &parsed); err == nil {
			if parsed["type"] == "gameStarted" {
				foundStarted1 = true
			}
		}
	}
	
	for _, msg := range messages2 {
		var parsed map[string]interface{}
		if err := json.Unmarshal(msg, &parsed); err == nil {
			if parsed["type"] == "gameStarted" {
				foundStarted2 = true
			}
		}
	}
	
	if !foundStarted1 {
		t.Error("Client 1 did not receive gameStarted message")
	}
	if !foundStarted2 {
		t.Error("Client 2 did not receive gameStarted message")
	}
	
	client1.close()
	client2.close()
}

func TestHub_PeekCard(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()
	
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)
	
	// Create game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Get game ID and join with second player
	messages1 := client1.getMessages()
	var joinedMsg GameJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	gameID := joinedMsg.GameState.ID
	
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Start the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "startGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	client1.clearMessages()
	
	// Peek at card 0
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "peekCard", "cardIndex": 0}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Should receive updated game state
	messages := client1.getMessages()
	if len(messages) == 0 {
		t.Fatal("Expected game state update after peeking")
	}
	
	// Find the game state message
	var gameStateMsg *GameStateUpdateMessage
	for _, msg := range messages {
		var parsed GameStateUpdateMessage
		if err := json.Unmarshal(msg, &parsed); err == nil && parsed.Type == "gameState" {
			gameStateMsg = &parsed
			break
		}
	}
	
	if gameStateMsg == nil {
		t.Fatal("Did not receive game state update")
	}
	
	// Check that player has peeked at 1 card
	player := gameStateMsg.GameState.Players[0]
	if len(player.RevealedCards) != 1 {
		t.Errorf("Expected 1 revealed card, got %d", len(player.RevealedCards))
	}
	
	if player.RevealedCards[0] != 0 {
		t.Errorf("Expected card 0 to be revealed, got %d", player.RevealedCards[0])
	}
	
	client1.close()
	client2.close()
}

func TestHub_GameFlow(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()
	
	// Create two clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)
	
	// Create game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Get game ID
	messages1 := client1.getMessages()
	var joinedMsg GameJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	gameID := joinedMsg.GameState.ID
	
	// Player 2 joins
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Start game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "startGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	client1.clearMessages()
	client2.clearMessages()
	
	// Player 1 draws a card
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "drawCard"}`),
		Sender:  hubClient1,
	})
	time.Sleep(50 * time.Millisecond)
	
	// Both players should receive updated game state
	messages1 = client1.getMessages()
	messages2 := client2.getMessages()
	
	if len(messages1) == 0 {
		t.Error("Player 1 did not receive game state update")
	}
	if len(messages2) == 0 {
		t.Error("Player 2 did not receive game state update")
	}
	
	// Player 1 discards the drawn card
	client1.clearMessages()
	client2.clearMessages()
	
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "discardDrawn"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Check for turn change message
	messages2 = client2.getMessages()
	foundTurnChange := false
	for _, msg := range messages2 {
		var parsed map[string]interface{}
		if err := json.Unmarshal(msg, &parsed); err == nil {
			if parsed["type"] == "turnChanged" {
				foundTurnChange = true
			}
		}
	}
	
	if !foundTurnChange {
		t.Error("Did not receive turn change notification")
	}
	
	client1.close()
	client2.close()
}

func TestHub_PlayerDisconnect(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()
	
	// Create two clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)
	
	// Create game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	
	// Get game ID
	messages1 := client1.getMessages()
	var joinedMsg GameJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	gameID := joinedMsg.GameState.ID
	
	// Player 2 joins
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)
	
	client2.clearMessages()
	
	// Player 1 disconnects
	golfHub.Unregister(hubClient1)
	time.Sleep(10 * time.Millisecond)
	
	// Player 2 should receive updated game state
	messages2 := client2.getMessages()
	if len(messages2) == 0 {
		t.Fatal("Player 2 did not receive update after player 1 disconnect")
	}
	
	// Check the game state
	var stateMsg GameStateUpdateMessage
	for _, msg := range messages2 {
		if err := json.Unmarshal(msg, &stateMsg); err == nil && stateMsg.Type == "gameState" {
			break
		}
	}
	
	if len(stateMsg.GameState.Players) != 1 {
		t.Errorf("Expected 1 player after disconnect, got %d", len(stateMsg.GameState.Players))
	}
	
	client2.close()
}

func TestHub_InvalidMessages(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()
	
	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	
	golfHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)
	
	tests := []struct {
		name string
		msg  string
	}{
		{
			name: "invalid JSON",
			msg:  `{"type":"createGame"`,
		},
		{
			name: "unknown message type",
			msg:  `{"type":"unknownType"}`,
		},
		{
			name: "join non-existent game",
			msg:  `{"type":"joinGame","gameId":"XXXXXX"}`,
		},
		{
			name: "start game when not in one",
			msg:  `{"type":"startGame"}`,
		},
		{
			name: "draw card when not in game",
			msg:  `{"type":"drawCard"}`,
		},
	}
	
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			client1.clearMessages()
			
			golfHub.GameMessage(hub.GameMessageData{
				Message: []byte(tt.msg),
				Sender:  hubClient1,
			})
			time.Sleep(10 * time.Millisecond)
			
			messages := client1.getMessages()
			// Should receive error message
			foundError := false
			for _, msg := range messages {
				var parsed ErrorMessage
				if err := json.Unmarshal(msg, &parsed); err == nil && parsed.Type == "error" {
					foundError = true
					break
				}
			}
			
			if !foundError && tt.name != "invalid JSON" {
				t.Errorf("Expected error message for %s", tt.name)
			}
		})
	}
	
	client1.close()
}