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
			json:    `{"type": "joinGame", "roomId": "ROOM1", "gameId": "ABC123"}`,
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

	// Client 1 creates room
	createMsg := `{"type": "createRoom"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Check client 1 received roomJoined message
	messages1 := client1.getMessages()
	if len(messages1) != 1 {
		t.Fatalf("Expected 1 message for client1, got %d", len(messages1))
	}

	var joinedMsg RoomJoinedMessage
	if err := json.Unmarshal(messages1[0], &joinedMsg); err != nil {
		t.Fatalf("Failed to parse roomJoined message: %v", err)
	}

	if joinedMsg.Type != "roomJoined" {
		t.Errorf("Expected roomJoined message, got %s", joinedMsg.Type)
	}

	roomID := joinedMsg.RoomState.ID
	if len(roomID) != 6 {
		t.Errorf("Expected 6-character room ID, got %s", roomID)
	}

	// Client 2 joins the room with a specific room ID
	client2.clearMessages()
	joinMsg := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Check client 2 received roomJoined message (and possibly a room state broadcast)
	messages2 := client2.getMessages()
	if len(messages2) == 0 {
		t.Fatal("Expected at least 1 message for client2")
	}

	var joined2Msg RoomJoinedMessage
	if err := json.Unmarshal(messages2[0], &joined2Msg); err != nil {
		t.Fatalf("Failed to parse roomJoined message: %v", err)
	}

	if joined2Msg.RoomState.ID != roomID {
		t.Errorf("Expected to join room %s, got %s", roomID, joined2Msg.RoomState.ID)
	}

	if len(joined2Msg.RoomState.Players) != 2 {
		t.Errorf("Expected 2 players in room, got %d", len(joined2Msg.RoomState.Players))
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

	// Create room, game and get both players in
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages1 := client1.getMessages()
	var joinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	roomID := joinedMsg.RoomState.ID

	// Both clients need to join the same game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
		Sender:  hubClient1,
	})

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
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

	// Get room ID and join both players to the same game
	messages1 := client1.getMessages()
	var roomJoinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &roomJoinedMsg)
	roomID := roomJoinedMsg.RoomState.ID

	// Both clients need to join the same game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
		Sender:  hubClient1,
	})

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Start the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "startGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get the actual game ID from the game state after starting
	startMessages := client1.getMessages()
	var gameID string
	for _, msg := range startMessages {
		var gameStateMsg GameStateUpdateMessage
		if err := json.Unmarshal(msg, &gameStateMsg); err == nil && gameStateMsg.Type == "gameState" {
			gameID = gameStateMsg.GameState.ID
			break
		}
	}
	if gameID == "" {
		t.Fatal("Could not find game ID after starting game")
	}

	// Verify game ID is different from room ID
	if gameID == roomID {
		t.Fatal("Game ID should be different from room ID")
	}

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

	// Get room ID and join both players to the same game
	messages1 := client1.getMessages()
	var roomJoinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &roomJoinedMsg)
	roomID := roomJoinedMsg.RoomState.ID

	// Both players join the same game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
		Sender:  hubClient1,
	})

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
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

	// Get room ID and join both players to the same game
	messages1 := client1.getMessages()
	var roomJoinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &roomJoinedMsg)
	roomID := roomJoinedMsg.RoomState.ID

	// Both players join the same game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
		Sender:  hubClient1,
	})

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	client2.clearMessages()

	// Player 1 disconnects
	golfHub.Unregister(hubClient1)
	time.Sleep(10 * time.Millisecond)

	// Player 2 should receive some kind of update after player 1 disconnect
	messages2 := client2.getMessages()
	if len(messages2) == 0 {
		t.Error("Player 2 did not receive update after player 1 disconnect")
	}

	// With the new multi-game architecture, we should receive a room state update
	// since the disconnecting player affects the room's player list
	var foundUpdate bool
	for _, msg := range messages2 {
		var roomStateMsg RoomStateUpdateMessage
		if err := json.Unmarshal(msg, &roomStateMsg); err == nil && roomStateMsg.Type == "roomStateUpdate" {
			foundUpdate = true
			// Verify the room still exists and has the remaining player
			if len(roomStateMsg.RoomState.Players) != 2 {
				t.Errorf("Expected 2 players in room (one disconnected), got %d", len(roomStateMsg.RoomState.Players))
			}
			break
		}
	}

	if !foundUpdate {
		t.Error("Expected to receive room state update after player disconnect")
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
			name: "join non-existent room",
			msg:  `{"type":"joinGame","roomId":"XXXXXX","gameId":"GAME1"}`,
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

// TestHub_MultiGameSupport tests the core multi-game functionality
func TestHub_MultiGameSupport(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()

	// Create mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	client3 := newMockClient("client3")
	client3.collectMessages()

	// Convert to hub clients
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	hubClient3 := &hub.Client{Hub: golfHub, Send: client3.send}

	// Register clients
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	golfHub.Register(hubClient3)
	time.Sleep(10 * time.Millisecond)

	// Client 1 creates a room
	createMsg := `{"type": "createGame"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Extract room ID from client1's response
	messages1 := client1.getMessages()
	if len(messages1) != 1 {
		t.Fatalf("Expected 1 message for client1, got %d", len(messages1))
	}

	var joinedMsg RoomJoinedMessage
	if err := json.Unmarshal(messages1[0], &joinedMsg); err != nil {
		t.Fatalf("Failed to parse roomJoined message: %v", err)
	}
	roomID := joinedMsg.RoomState.ID

	// Client 2 joins the room with game ID "GAME1"
	joinMsg2 := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg2),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 3 joins the room with game ID "GAME2" (different game)
	joinMsg3 := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME2"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg3),
		Sender:  hubClient3,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify clients are in different games
	hub := golfHub.(*GolfHub)
	hub.mu.RLock()
	context1 := hub.clientToGame[hubClient1]
	context2 := hub.clientToGame[hubClient2]
	context3 := hub.clientToGame[hubClient3]

	if context1.RoomID != roomID || context2.RoomID != roomID || context3.RoomID != roomID {
		t.Error("All clients should be in the same room")
	}

	if context1.GameID != "" {
		t.Error("Client1 should not be in a specific game yet (created room, not joined game)")
	}

	if context2.GameID != "GAME1" {
		t.Errorf("Client2 should be in GAME1, got %s", context2.GameID)
	}

	if context3.GameID != "GAME2" {
		t.Errorf("Client3 should be in GAME2, got %s", context3.GameID)
	}

	// Check that multiple games exist in the room
	room := hub.rooms[roomID]
	if len(room.Games) != 2 {
		t.Errorf("Expected 2 games in room, got %d", len(room.Games))
	}

	if _, exists := room.Games["GAME1"]; !exists {
		t.Error("GAME1 should exist in room")
	}

	if _, exists := room.Games["GAME2"]; !exists {
		t.Error("GAME2 should exist in room")
	}
	hub.mu.RUnlock()

	client1.close()
	client2.close()
	client3.close()
}

// TestHub_GameIsolation tests that games are properly isolated
func TestHub_GameIsolation(t *testing.T) {
	golfHub := NewGolfHub()
	go golfHub.Run()

	// Create mock clients
	client1 := newMockClient("client1")
	client1.collectMessages()
	client2 := newMockClient("client2")
	client2.collectMessages()
	client3 := newMockClient("client3")
	client3.collectMessages()
	client4 := newMockClient("client4")
	client4.collectMessages()

	// Convert to hub clients
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	hubClient3 := &hub.Client{Hub: golfHub, Send: client3.send}
	hubClient4 := &hub.Client{Hub: golfHub, Send: client4.send}

	// Register clients
	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	golfHub.Register(hubClient3)
	golfHub.Register(hubClient4)
	time.Sleep(10 * time.Millisecond)

	// Client 1 creates a room
	createMsg := `{"type": "createGame"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Extract room ID
	messages1 := client1.getMessages()
	var joinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	roomID := joinedMsg.RoomState.ID

	// Create two separate games
	// Game 1: Client1 and Client2
	joinMsg1 := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME1"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg1),
		Sender:  hubClient1,
	})

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg1),
		Sender:  hubClient2,
	})

	// Game 2: Client3 and Client4
	joinMsg2 := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "GAME2"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg2),
		Sender:  hubClient3,
	})

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg2),
		Sender:  hubClient4,
	})
	time.Sleep(20 * time.Millisecond)

	// Clear messages
	client1.clearMessages()
	client2.clearMessages()
	client3.clearMessages()
	client4.clearMessages()

	// Start game 1
	startMsg := `{"type": "startGame"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(startMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Only clients in game 1 should receive game started message
	messages1After := client1.getMessages()
	messages2After := client2.getMessages()
	messages3After := client3.getMessages()
	messages4After := client4.getMessages()

	if len(messages1After) == 0 || len(messages2After) == 0 {
		t.Error("Clients 1 and 2 should have received game started messages")
	}

	if len(messages3After) > 0 || len(messages4After) > 0 {
		t.Error("Clients 3 and 4 should not have received any messages (they're in a different game)")
	}

	client1.close()
	client2.close()
	client3.close()
	client4.close()
}

// TestHub_RequiredGameID tests that gameId is now required for joining
func TestHub_RequiredGameID(t *testing.T) {
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

	// Client 1 creates room
	createMsg := `{"type": "createGame"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Extract room ID
	messages1 := client1.getMessages()
	var joinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	roomID := joinedMsg.RoomState.ID

	// Try to join without gameId - should fail
	joinMsgNoGameID := `{"type": "joinGame", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsgNoGameID),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Check that client2 received an error
	messages2 := client2.getMessages()
	if len(messages2) != 1 {
		t.Fatalf("Expected 1 error message for client2, got %d", len(messages2))
	}

	var errorMsg ErrorMessage
	if err := json.Unmarshal(messages2[0], &errorMsg); err != nil {
		t.Fatalf("Failed to parse error message: %v", err)
	}

	if errorMsg.Type != "error" {
		t.Errorf("Expected error message, got %s", errorMsg.Type)
	}

	if errorMsg.Message != "Game ID is required" {
		t.Errorf("Expected 'Game ID is required' error, got '%s'", errorMsg.Message)
	}

	client1.close()
	client2.close()
}

// TestHub_GameCleanup tests that completed games are cleaned up
func TestHub_GameCleanup(t *testing.T) {
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

	// Create room and join game
	createMsg := `{"type": "createGame"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages1 := client1.getMessages()
	var joinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	roomID := joinedMsg.RoomState.ID

	// Both clients join the same game
	joinMsg := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "TESTGAME"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient1,
	})
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinMsg),
		Sender:  hubClient2,
	})
	time.Sleep(20 * time.Millisecond)

	// Verify game exists
	hub := golfHub.(*GolfHub)
	hub.mu.RLock()
	room := hub.rooms[roomID]
	if len(room.Games) != 1 {
		t.Errorf("Expected 1 game before completion, got %d", len(room.Games))
	}
	game := room.Games["TESTGAME"]
	if game == nil {
		t.Fatal("Game should exist before completion")
	}
	hub.mu.RUnlock()

	// Simulate game completion by setting game phase to "ended"
	game.mu.Lock()
	game.state.GamePhase = "ended"
	game.mu.Unlock()

	// Trigger game completion
	hub.mu.Lock()
	err := hub.completeGameInRoom(roomID, "TESTGAME")
	hub.mu.Unlock()

	if err != nil {
		t.Fatalf("Game completion failed: %v", err)
	}

	// Verify game was removed from active games
	hub.mu.RLock()
	room = hub.rooms[roomID]
	if len(room.Games) != 0 {
		t.Errorf("Expected 0 games after completion, got %d", len(room.Games))
	}

	// Verify game was added to history
	if len(room.GameHistory) != 1 {
		t.Errorf("Expected 1 game in history, got %d", len(room.GameHistory))
	}
	hub.mu.RUnlock()

	client1.close()
	client2.close()
}
