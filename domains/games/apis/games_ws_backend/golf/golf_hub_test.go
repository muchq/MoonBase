package golf

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

// authenticateMockClient sends an authenticate message and clears the response.
// Must be called after Register and before any other game messages.
func authenticateMockClient(golfHub hub.Hub, hubClient *hub.Client, mockCli *mockClient) {
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"authenticate","sessionToken":""}`),
		Sender:  hubClient,
	})
	time.Sleep(10 * time.Millisecond)
	mockCli.clearMessages()
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

// Auth Tests

func TestHub_AuthenticationRequired(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
	go golfHub.Run()

	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}

	golfHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)

	// Try to create room without authenticating
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages := client1.getMessages()
	if len(messages) != 1 {
		t.Fatalf("Expected 1 message, got %d", len(messages))
	}

	var errMsg ErrorMessage
	json.Unmarshal(messages[0], &errMsg)
	if errMsg.Type != "error" || errMsg.Message != "Must authenticate first" {
		t.Errorf("Expected 'Must authenticate first' error, got: %s", errMsg.Message)
	}

	client1.close()
}

func TestHub_AuthenticationFlow(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
	go golfHub.Run()

	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}

	golfHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)

	// Authenticate with empty token (new session)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"authenticate","sessionToken":""}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages := client1.getMessages()
	if len(messages) != 1 {
		t.Fatalf("Expected 1 message, got %d", len(messages))
	}

	var authMsg AuthenticatedMessage
	json.Unmarshal(messages[0], &authMsg)

	if authMsg.Type != "authenticated" {
		t.Errorf("Expected authenticated message, got %s", authMsg.Type)
	}
	if authMsg.SessionToken == "" {
		t.Error("Session token should not be empty")
	}
	if authMsg.PlayerID == "" {
		t.Error("Player ID should not be empty")
	}
	if authMsg.Reconnected {
		t.Error("Should not be a reconnection")
	}

	client1.close()
}

func TestHub_TokenReconnection(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
	go golfHub.Run()

	// First client connects and authenticates
	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}

	golfHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"authenticate","sessionToken":""}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Extract token and playerID
	messages := client1.getMessages()
	var authMsg AuthenticatedMessage
	json.Unmarshal(messages[0], &authMsg)
	token := authMsg.SessionToken
	playerID := authMsg.PlayerID

	client1.clearMessages()

	// Create a room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages = client1.getMessages()
	var roomJoined RoomJoinedMessage
	json.Unmarshal(messages[0], &roomJoined)
	roomID := roomJoined.RoomState.ID

	// Disconnect
	client1.mu.Lock()
	client1.closed = true
	client1.mu.Unlock()
	golfHub.Unregister(hubClient1)
	time.Sleep(50 * time.Millisecond)

	// Reconnect with stored token
	client2 := newMockClient("client2")
	client2.collectMessages()
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}

	golfHub.Register(hubClient2)
	time.Sleep(10 * time.Millisecond)

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"authenticate","sessionToken":"` + token + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(100 * time.Millisecond) // Extra time for async state restoration

	messages2 := client2.getMessages()
	if len(messages2) < 1 {
		t.Fatal("Expected at least 1 message on reconnect")
	}

	// First message should be authenticated with reconnected=true
	var reconnectAuth AuthenticatedMessage
	json.Unmarshal(messages2[0], &reconnectAuth)

	if reconnectAuth.Type != "authenticated" {
		t.Errorf("Expected authenticated message, got %s", reconnectAuth.Type)
	}
	if !reconnectAuth.Reconnected {
		t.Error("Should be marked as reconnection")
	}
	if reconnectAuth.PlayerID != playerID {
		t.Errorf("Expected playerID %s, got %s", playerID, reconnectAuth.PlayerID)
	}

	// Should also receive roomJoined with same room
	foundRoom := false
	for _, msg := range messages2 {
		var parsed map[string]interface{}
		json.Unmarshal(msg, &parsed)
		if parsed["type"] == "roomJoined" {
			if rs, ok := parsed["roomState"].(map[string]interface{}); ok {
				if rs["id"] == roomID {
					foundRoom = true
				}
			}
		}
	}
	if !foundRoom {
		t.Errorf("Expected to be restored to room %s on reconnect", roomID)
	}

	client2.close()
}

func TestHub_AlgValidation(t *testing.T) {
	tm := NewTokenManagerWithSecret([]byte("test-secret"))

	token, err := tm.CreateToken("player-1", 24*time.Hour)
	if err != nil {
		t.Fatalf("Failed to create token: %v", err)
	}

	// Valid token should work
	playerID, err := tm.ValidateToken(token)
	if err != nil {
		t.Fatalf("Valid token should validate: %v", err)
	}
	if playerID != "player-1" {
		t.Errorf("Expected player-1, got %s", playerID)
	}

	// Tampered token should fail
	_, err = tm.ValidateToken(token + "tampered")
	if err == nil {
		t.Error("Tampered token should fail validation")
	}

	// Token from different secret should fail
	tm2 := NewTokenManagerWithSecret([]byte("other-secret"))
	_, err = tm2.ValidateToken(token)
	if err == nil {
		t.Error("Token from different secret should fail")
	}
}

// Hub Integration Tests

func TestHub_CreateAndJoinRoom(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

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
	joinMsg := `{"type": "joinRoom", "roomId": "` + roomID + `"}`
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

	if joined2Msg.Type != "roomJoined" {
		t.Errorf("Expected roomJoined got %s", joined2Msg.Type)
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

func TestHub_CreateAndJoinGame(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Client 1 creates room
	createRoomMsg := `{"type": "createRoom"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createRoomMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Check client 1 received roomJoined message
	messages1 := client1.getMessages()
	if len(messages1) != 1 {
		t.Fatalf("Expected 1 message for client1, got %d", len(messages1))
	}

	var createdRoomMsg RoomJoinedMessage
	if err := json.Unmarshal(messages1[0], &createdRoomMsg); err != nil {
		t.Fatalf("Failed to parse roomJoined message: %v", err)
	}

	if createdRoomMsg.Type != "roomJoined" {
		t.Errorf("Expected roomJoined message, got %s", createdRoomMsg.Type)
	}

	roomID := createdRoomMsg.RoomState.ID
	if len(roomID) != 6 {
		t.Errorf("Expected 6-character room ID, got %s", roomID)
	}

	// Client 2 joins the room with a specific room ID
	client2.clearMessages()
	joinRoomMsg := `{"type": "joinRoom", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinRoomMsg),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Check client 2 received roomJoined message (and possibly a room state broadcast)
	messages2 := client2.getMessages()
	if len(messages2) == 0 {
		t.Fatal("Expected at least 1 message for client2")
	}

	var joinedRoomMsg RoomJoinedMessage
	if err := json.Unmarshal(messages2[0], &joinedRoomMsg); err != nil {
		t.Fatalf("Failed to parse roomJoined message: %v", err)
	}

	if joinedRoomMsg.RoomState.ID != roomID {
		t.Errorf("Expected to join room %s, got %s", roomID, joinedRoomMsg.RoomState.ID)
	}

	if len(joinedRoomMsg.RoomState.Players) != 2 {
		t.Errorf("Expected 2 players in room, got %d", len(joinedRoomMsg.RoomState.Players))
	}

	// Client 1 creates Game
	client1.clearMessages()
	createGameMsg := `{"type": "createGame", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createGameMsg),
		Sender:  hubClient1,
	})
	time.Sleep(100 * time.Millisecond)

	// Check client 1 received gameJoined message and roomStateUpdate message
	gameCreated1 := client1.getMessages()
	if len(gameCreated1) < 1 {
		t.Fatalf("Expected at least 1 message for client1, got %d", len(gameCreated1))
	}

	// Look for gameJoined message
	var createdGameMsg GameJoinedMessage
	foundGameJoined := false
	for _, msg := range gameCreated1 {
		var testMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &testMsg); err == nil && testMsg.Type == "gameJoined" {
			createdGameMsg = testMsg
			foundGameJoined = true
			break
		}
	}
	
	if !foundGameJoined {
		t.Fatal("Expected to find gameJoined message")
	}

	if createdGameMsg.Type != "gameJoined" {
		t.Errorf("Expected gameJoined message, got %s", createdGameMsg.Type)
	}

	gameID := createdGameMsg.GameState.ID
	if len(gameID) < 4 {
		t.Errorf("Expected game ID with at least 4 characters, got %s", gameID)
	}

	// Client 2 joins the game that client 1 created
	client2.clearMessages()
	joinGameMsg := `{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinGameMsg),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Check client 2 received roomStateUpdate message (broadcast when joining game)
	messages4 := client2.getMessages()
	if len(messages4) == 0 {
		t.Fatal("Expected at least 1 message for client2")
	}

	// Look for room state update in the messages (since joining a game broadcasts room state)
	foundRoomUpdate := false
	for _, msg := range messages4 {
		var roomUpdateMsg RoomStateUpdateMessage
		if err := json.Unmarshal(msg, &roomUpdateMsg); err == nil && roomUpdateMsg.Type == "roomStateUpdate" {
			if roomUpdateMsg.RoomState.ID == roomID {
				foundRoomUpdate = true
				if len(roomUpdateMsg.RoomState.Players) != 2 {
					t.Errorf("Expected 2 players in room, got %d", len(roomUpdateMsg.RoomState.Players))
				}
				break
			}
		}
	}

	if !foundRoomUpdate {
		t.Error("Expected to receive room state update after joining game")
	}

	client1.close()
	client2.close()
}

func TestHub_StartGame(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Player 1 creates room (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages1 := client1.getMessages()
	var joinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	roomID := joinedMsg.RoomState.ID

	// Player 2 joins the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Player 1 creates game (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get the game ID from player 1's gameJoined message
	gameMessages := client1.getMessages()
	var gameID string
	for _, msg := range gameMessages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			gameID = gameJoinedMsg.GameState.ID
			break
		}
	}

	// Player 2 joins the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify player 2 received gameJoined message
	player2Messages := client2.getMessages()
	foundGameJoined := false
	var player2GameJoinedMsg GameJoinedMessage
	
	for _, msg := range player2Messages {
		var testMsg struct {
			Type string `json:"type"`
		}
		if err := json.Unmarshal(msg, &testMsg); err == nil && testMsg.Type == "gameJoined" {
			if err := json.Unmarshal(msg, &player2GameJoinedMsg); err == nil {
				foundGameJoined = true
				break
			}
		}
	}
	
	if !foundGameJoined {
		t.Fatal("Player 2 should have received gameJoined message when joining the game")
	}
	
	if player2GameJoinedMsg.Type != "gameJoined" {
		t.Errorf("Expected gameJoined message for player 2, got %s", player2GameJoinedMsg.Type)
	}
	
	if player2GameJoinedMsg.GameState.ID != gameID {
		t.Errorf("Expected game ID %s in gameJoined message, got %s", gameID, player2GameJoinedMsg.GameState.ID)
	}
	
	if len(player2GameJoinedMsg.GameState.Players) != 2 {
		t.Errorf("Expected 2 players in game state, got %d", len(player2GameJoinedMsg.GameState.Players))
	}

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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Player 1 creates room (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get room ID from player 1's roomJoined message
	messages1 := client1.getMessages()
	var roomJoinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &roomJoinedMsg)
	roomID := roomJoinedMsg.RoomState.ID

	// Player 2 joins the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Player 1 creates game (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get the game ID from player 1's gameJoined message
	gameMessages := client1.getMessages()
	var gameID string
	for _, msg := range gameMessages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			gameID = gameJoinedMsg.GameState.ID
			break
		}
	}

	// Player 2 joins the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Start the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "startGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify game ID is different from room ID (we already have gameID from earlier)
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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Player 1 creates room (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get room ID from player 1's roomJoined message
	messages1 := client1.getMessages()
	var roomJoinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &roomJoinedMsg)
	roomID := roomJoinedMsg.RoomState.ID

	// Player 2 joins the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Player 1 creates game (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get the game ID from player 1's gameJoined message
	gameMessages := client1.getMessages()
	var gameID string
	for _, msg := range gameMessages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			gameID = gameJoinedMsg.GameState.ID
			break
		}
	}

	// Player 2 joins the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Player 1 creates room (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get room ID from player 1's roomJoined message
	messages1 := client1.getMessages()
	var roomJoinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &roomJoinedMsg)
	roomID := roomJoinedMsg.RoomState.ID

	// Player 2 joins the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Player 1 creates game (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get the game ID from player 1's gameJoined message
	gameMessages := client1.getMessages()
	var gameID string
	for _, msg := range gameMessages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			gameID = gameJoinedMsg.GameState.ID
			break
		}
	}

	// Player 2 joins the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
	go golfHub.Run()

	client1 := newMockClient("client1")
	client1.collectMessages()
	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}

	golfHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)
	authenticateMockClient(golfHub, hubClient1, client1)

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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)
	authenticateMockClient(golfHub, hubClient3, client3)

	// Client 1 creates a room
	createMsg := `{"type": "createRoom"}`
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

	// Client 2 joins the room first
	joinRoomMsg2 := `{"type": "joinRoom", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinRoomMsg2),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 3 joins the room first  
	joinRoomMsg3 := `{"type": "joinRoom", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(joinRoomMsg3),
		Sender:  hubClient3,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 2 creates game "GAME1"
	createGame1Msg := `{"type": "createGame", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createGame1Msg),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 3 creates game "GAME2"  
	createGame2Msg := `{"type": "createGame", "roomId": "` + roomID + `"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createGame2Msg),
		Sender:  hubClient3,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify clients are in different games
	hub := golfHub.(*GolfHub)
	hub.mu.RLock()
	context1 := hub.clientContexts[hubClient1]
	context2 := hub.clientContexts[hubClient2]
	context3 := hub.clientContexts[hubClient3]

	if context1.RoomID != roomID || context2.RoomID != roomID || context3.RoomID != roomID {
		t.Error("All clients should be in the same room")
	}

	if context1.GameID != "" {
		t.Error("Client1 should not be in a specific game yet (created room, not joined game)")
	}

	// Client2 and Client3 should be in different games (whatever IDs were auto-generated)
	if context2.GameID == "" {
		t.Error("Client2 should be in a game after creating one")
	}

	if context3.GameID == "" {
		t.Error("Client3 should be in a game after creating one")
	}

	if context2.GameID == context3.GameID {
		t.Error("Client2 and Client3 should be in different games")
	}

	// Check that multiple games exist in the room
	room := hub.rooms[roomID]
	if len(room.Games) != 2 {
		t.Errorf("Expected 2 games in room, got %d", len(room.Games))
	}

	// Verify the specific games exist (using the actual generated IDs)
	if _, exists := room.Games[context2.GameID]; !exists {
		t.Errorf("Game %s should exist in room", context2.GameID)
	}

	if _, exists := room.Games[context3.GameID]; !exists {
		t.Errorf("Game %s should exist in room", context3.GameID)
	}
	hub.mu.RUnlock()

	client1.close()
	client2.close()
	client3.close()
}

// TestHub_GameIsolation tests that games are properly isolated
func TestHub_GameIsolation(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)
	authenticateMockClient(golfHub, hubClient3, client3)
	authenticateMockClient(golfHub, hubClient4, client4)

	// Client 1 creates a room
	createMsg := `{"type": "createRoom"}`
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

	// All other clients join the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient3,
	})
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient4,
	})
	time.Sleep(20 * time.Millisecond)

	// Create two separate games
	// Game 1: Client1 creates, Client2 joins
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get game1 ID from client1's response
	game1Messages := client1.getMessages()
	var game1ID string
	for _, msg := range game1Messages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			game1ID = gameJoinedMsg.GameState.ID
			break
		}
	}

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + game1ID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Game 2: Client3 creates, Client4 joins
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient3,
	})
	time.Sleep(10 * time.Millisecond)

	// Get game2 ID from client3's response
	game2Messages := client3.getMessages()
	var game2ID string
	for _, msg := range game2Messages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			game2ID = gameJoinedMsg.GameState.ID
			break
		}
	}

	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + game2ID + `"}`),
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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Client 1 creates room
	createMsg := `{"type": "createRoom"}`
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
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Player 1 creates room (automatically joins)
	createMsg := `{"type": "createRoom"}`
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(createMsg),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	messages1 := client1.getMessages()
	var joinedMsg RoomJoinedMessage
	json.Unmarshal(messages1[0], &joinedMsg)
	roomID := joinedMsg.RoomState.ID

	// Player 2 joins the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Player 1 creates game (automatically joins)
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get the game ID from player 1's gameJoined message
	gameMessages := client1.getMessages()
	var gameID string
	for _, msg := range gameMessages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			gameID = gameJoinedMsg.GameState.ID
			break
		}
	}

	// Player 2 joins the game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
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
	game := room.Games[gameID]
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
	err := hub.completeGameInRoom(roomID, gameID)
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

// Double Join Prevention Tests

func TestHub_JoinGameTwice(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
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
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)

	// Client 1 creates room
	createMsg := `{"type": "createRoom"}`
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

	// Client 2 joins the room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 1 creates a game
	client1.clearMessages()
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "createGame", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Get game ID from client1's response
	game1Messages := client1.getMessages()
	var gameID string
	for _, msg := range game1Messages {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			gameID = gameJoinedMsg.GameState.ID
			break
		}
	}

	if gameID == "" {
		t.Fatal("Failed to get game ID from client1")
	}

	// Client 2 joins the game
	client2.clearMessages()
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify client 2 successfully joined
	messages2 := client2.getMessages()
	foundGameJoined := false
	for _, msg := range messages2 {
		var gameJoinedMsg GameJoinedMessage
		if err := json.Unmarshal(msg, &gameJoinedMsg); err == nil && gameJoinedMsg.Type == "gameJoined" {
			foundGameJoined = true
			break
		}
	}
	if !foundGameJoined {
		t.Fatal("Client 2 should have received gameJoined message")
	}

	// Now client 2 tries to join the same game again
	client2.clearMessages()
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinGame", "roomId": "` + roomID + `", "gameId": "` + gameID + `"}`),
		Sender:  hubClient2,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 2 should receive an error message
	messages2 = client2.getMessages()
	foundError := false
	var errorMsg ErrorMessage
	for _, msg := range messages2 {
		if err := json.Unmarshal(msg, &errorMsg); err == nil && errorMsg.Type == "error" {
			foundError = true
			break
		}
	}

	if !foundError {
		t.Error("Expected error message when player tries to join game twice")
	}

	if errorMsg.Message != "player already in game" {
		t.Errorf("Expected 'player already in game' error, got: %s", errorMsg.Message)
	}

	// Verify that the game still has only 2 players
	hub := golfHub.(*GolfHub)
	hub.mu.RLock()
	room := hub.rooms[roomID]
	game := room.Games[gameID]
	if len(game.state.Players) != 2 {
		t.Errorf("Expected 2 players in game after double-join attempt, got %d", len(game.state.Players))
	}
	hub.mu.RUnlock()

	client1.close()
	client2.close()
}

func TestHub_JoinRoomTwice(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
	go golfHub.Run()

	client1 := newMockClient("client1")
	client1.collectMessages()

	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}

	golfHub.Register(hubClient1)
	time.Sleep(10 * time.Millisecond)
	authenticateMockClient(golfHub, hubClient1, client1)

	// Client 1 creates room (automatically joins)
	createMsg := `{"type": "createRoom"}`
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

	// Verify client 1 successfully created and joined the room
	if joinedMsg.Type != "roomJoined" {
		t.Fatal("Client 1 should have received roomJoined message")
	}

	if len(joinedMsg.RoomState.Players) != 1 {
		t.Errorf("Expected 1 player in room, got %d", len(joinedMsg.RoomState.Players))
	}

	// Now client 1 tries to join the same room again
	client1.clearMessages()
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type": "joinRoom", "roomId": "` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	// Client 1 should receive an error message
	messages1 = client1.getMessages()
	foundError := false
	var errorMsg ErrorMessage
	for _, msg := range messages1 {
		if err := json.Unmarshal(msg, &errorMsg); err == nil && errorMsg.Type == "error" {
			foundError = true
			break
		}
	}

	if !foundError {
		t.Error("Expected error message when player tries to join room twice")
	}

	if errorMsg.Message != "player already in room" {
		t.Errorf("Expected 'player already in room' error, got: %s", errorMsg.Message)
	}

	// Verify that the room still has only 1 player
	hub := golfHub.(*GolfHub)
	hub.mu.RLock()
	room := hub.rooms[roomID]
	if len(room.Players) != 1 {
		t.Errorf("Expected 1 player in room after double-join attempt, got %d", len(room.Players))
	}
	hub.mu.RUnlock()

	client1.close()
}

// TestGetClientID_UsesIDFieldOverRemoteAddr verifies that when clients connect
// through a proxy, their server-assigned UUID is used for identification rather
// than RemoteAddr. Without this, two clients sharing the same proxy IP would
// collide and be treated as the same player.
func TestGetClientID_UsesIDFieldOverRemoteAddr(t *testing.T) {
	t.Run("prefers ID field when set", func(t *testing.T) {
		client := &hub.Client{ID: "server-assigned-uuid", Send: make(chan []byte, 1)}
		if got := getClientID(client); got != "server-assigned-uuid" {
			t.Errorf("expected server-assigned-uuid, got %s", got)
		}
	})

	t.Run("two clients with same RemoteAddr get distinct IDs via ID field", func(t *testing.T) {
		// Simulates the proxy collision scenario: both clients have nil Conn
		// but differ only by their server-assigned ID.
		client1 := &hub.Client{ID: "uuid-1", Send: make(chan []byte, 1)}
		client2 := &hub.Client{ID: "uuid-2", Send: make(chan []byte, 1)}
		if getClientID(client1) == getClientID(client2) {
			t.Error("clients with different IDs must not share a client ID")
		}
	})

	t.Run("falls back to test-client pointer when ID and Conn are both unset", func(t *testing.T) {
		client := &hub.Client{Send: make(chan []byte, 1)}
		id := getClientID(client)
		if id == "" {
			t.Error("expected non-empty fallback ID for test client")
		}
	})
}

func TestHub_LeaveGame(t *testing.T) {
	golfHub := NewGolfHub(&players.DeterministicIDGenerator{})
	go golfHub.Run()

	// Create 3 clients
	client1 := newMockClient("c1")
	client1.collectMessages()
	client2 := newMockClient("c2")
	client2.collectMessages()
	client3 := newMockClient("c3")
	client3.collectMessages()

	hubClient1 := &hub.Client{Hub: golfHub, Send: client1.send}
	hubClient2 := &hub.Client{Hub: golfHub, Send: client2.send}
	hubClient3 := &hub.Client{Hub: golfHub, Send: client3.send}

	golfHub.Register(hubClient1)
	golfHub.Register(hubClient2)
	golfHub.Register(hubClient3)
	time.Sleep(10 * time.Millisecond)
	authenticateMockClient(golfHub, hubClient1, client1)
	authenticateMockClient(golfHub, hubClient2, client2)
	authenticateMockClient(golfHub, hubClient3, client3)

	// Client 1 creates room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"createRoom"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)

	msgs := client1.getMessages()
	var roomMsg RoomJoinedMessage
	json.Unmarshal(msgs[0], &roomMsg)
	roomID := roomMsg.RoomState.ID
	client1.clearMessages()

	// Clients 2 and 3 join room
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"joinRoom","roomId":"` + roomID + `"}`),
		Sender:  hubClient2,
	})
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"joinRoom","roomId":"` + roomID + `"}`),
		Sender:  hubClient3,
	})
	time.Sleep(10 * time.Millisecond)
	client1.clearMessages()
	client2.clearMessages()
	client3.clearMessages()

	// Client 1 creates game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"createGame","roomId":"` + roomID + `"}`),
		Sender:  hubClient1,
	})
	time.Sleep(100 * time.Millisecond)

	msgs = client1.getMessages()
	var gameMsg GameJoinedMessage
	for _, m := range msgs {
		if json.Unmarshal(m, &gameMsg) == nil && gameMsg.Type == "gameJoined" {
			break
		}
	}
	gameID := gameMsg.GameState.ID
	client1.clearMessages()
	client2.clearMessages()
	client3.clearMessages()

	// Clients 2 and 3 join game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"joinGame","roomId":"` + roomID + `","gameId":"` + gameID + `"}`),
		Sender:  hubClient2,
	})
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"joinGame","roomId":"` + roomID + `","gameId":"` + gameID + `"}`),
		Sender:  hubClient3,
	})
	time.Sleep(100 * time.Millisecond)
	client1.clearMessages()
	client2.clearMessages()
	client3.clearMessages()

	// Start game
	golfHub.GameMessage(hub.GameMessageData{
		Message: []byte(`{"type":"startGame"}`),
		Sender:  hubClient1,
	})
	time.Sleep(10 * time.Millisecond)
	client1.clearMessages()
	client2.clearMessages()
	client3.clearMessages()

	t.Run("leave game returns player to room", func(t *testing.T) {
		// Client 3 leaves the game
		golfHub.GameMessage(hub.GameMessageData{
			Message: []byte(`{"type":"leaveGame"}`),
			Sender:  hubClient3,
		})
		time.Sleep(100 * time.Millisecond)

		// Client 3's context should have no game
		golfHub.(*GolfHub).mu.RLock()
		ctx3 := golfHub.(*GolfHub).clientContexts[hubClient3]
		golfHub.(*GolfHub).mu.RUnlock()

		if ctx3 == nil {
			t.Fatal("Expected client3 context to still exist")
		}
		if ctx3.GameID != "" {
			t.Errorf("Expected empty GameID after leaving, got %s", ctx3.GameID)
		}
		if ctx3.RoomID != roomID {
			t.Errorf("Expected RoomID %s, got %s", roomID, ctx3.RoomID)
		}
	})

	t.Run("game still active with 2 players", func(t *testing.T) {
		golfHub.(*GolfHub).mu.RLock()
		room := golfHub.(*GolfHub).rooms[roomID]
		game := room.Games[gameID]
		golfHub.(*GolfHub).mu.RUnlock()

		if game == nil {
			t.Fatal("Expected game to still exist")
		}
		if game.state.GamePhase == "ended" {
			t.Error("Game should not have ended — still 2 players")
		}
	})

	t.Run("leave game ends game when fewer than 2 players remain", func(t *testing.T) {
		client1.clearMessages()
		client2.clearMessages()

		// Client 2 leaves — only 1 player left, game should end
		golfHub.GameMessage(hub.GameMessageData{
			Message: []byte(`{"type":"leaveGame"}`),
			Sender:  hubClient2,
		})
		time.Sleep(100 * time.Millisecond)

		// Check that client 1 received a gameEnded message
		msgs := client1.getMessages()
		foundGameEnded := false
		for _, m := range msgs {
			var parsed map[string]interface{}
			if json.Unmarshal(m, &parsed) == nil && parsed["type"] == "gameEnded" {
				foundGameEnded = true
				break
			}
		}
		if !foundGameEnded {
			t.Error("Expected client1 to receive gameEnded message")
		}
	})

	t.Run("leave game when not in a game returns error", func(t *testing.T) {
		client3.clearMessages()
		golfHub.GameMessage(hub.GameMessageData{
			Message: []byte(`{"type":"leaveGame"}`),
			Sender:  hubClient3,
		})
		time.Sleep(10 * time.Millisecond)

		msgs := client3.getMessages()
		foundError := false
		for _, m := range msgs {
			var parsed map[string]interface{}
			if json.Unmarshal(m, &parsed) == nil && parsed["type"] == "error" {
				foundError = true
				break
			}
		}
		if !foundError {
			t.Error("Expected error when leaving game while not in one")
		}
	})
}
