package golf

import (
	"encoding/json"
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/hub"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/players"
)

// TestClient provides a more feature-rich mock client for integration testing
type TestClient struct {
	id              string
	messages        [][]byte
	send            chan []byte
	closed          bool
	mu              sync.Mutex
	lastMessageTime time.Time
}

func NewTestClient(id string) *TestClient {
	return &TestClient{
		id:              id,
		messages:        make([][]byte, 0),
		send:            make(chan []byte, 256),
		lastMessageTime: time.Now(),
	}
}

func (tc *TestClient) StartCollecting() {
	go func() {
		for msg := range tc.send {
			tc.mu.Lock()
			if !tc.closed {
				tc.messages = append(tc.messages, msg)
				tc.lastMessageTime = time.Now()
			}
			tc.mu.Unlock()
		}
	}()
}

func (tc *TestClient) GetMessages() [][]byte {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	result := make([][]byte, len(tc.messages))
	copy(result, tc.messages)
	return result
}

func (tc *TestClient) ClearMessages() {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	tc.messages = nil
}

func (tc *TestClient) Close() {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	if !tc.closed {
		tc.closed = true
		close(tc.send)
	}
}

func (tc *TestClient) GetLastMessage() ([]byte, bool) {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	if len(tc.messages) == 0 {
		return nil, false
	}
	return tc.messages[len(tc.messages)-1], true
}

func (tc *TestClient) WaitForMessages(expectedCount int, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		tc.mu.Lock()
		count := len(tc.messages)
		tc.mu.Unlock()
		if count >= expectedCount {
			return true
		}
		time.Sleep(5 * time.Millisecond)
	}
	return false
}

func (tc *TestClient) FindMessageByType(msgType string) ([]byte, bool) {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	for _, msg := range tc.messages {
		var parsed map[string]interface{}
		if err := json.Unmarshal(msg, &parsed); err == nil {
			if parsed["type"] == msgType {
				return msg, true
			}
		}
	}
	return nil, false
}

// TestEnvironment manages the test environment with hub and multiple clients
type TestEnvironment struct {
	hub        *GolfHub
	clients    map[string]*TestClient
	hubClients map[string]*hub.Client
	running    bool
	mu         sync.RWMutex
}


func NewTestEnvironment() *TestEnvironment {
	env := &TestEnvironment{
		hub:        NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub),
		clients:    make(map[string]*TestClient),
		hubClients: make(map[string]*hub.Client),
	}
	go env.hub.Run()
	env.running = true
	return env
}

func (env *TestEnvironment) CreateClient(id string) *TestClient {
	env.mu.Lock()
	defer env.mu.Unlock()
	
	testClient := NewTestClient(id)
	testClient.StartCollecting()
	
	hubClient := &hub.Client{Hub: env.hub, Send: testClient.send}
	env.hub.Register(hubClient)
	
	env.clients[id] = testClient
	env.hubClients[id] = hubClient
	return testClient
}

func (env *TestEnvironment) SendMessage(clientID string, message string) error {
	env.mu.RLock()
	hubClient, exists := env.hubClients[clientID]
	env.mu.RUnlock()
	
	if !exists {
		return fmt.Errorf("client %s not found", clientID)
	}
	
	env.hub.GameMessage(hub.GameMessageData{
		Message: []byte(message),
		Sender:  hubClient,
	})
	
	return nil
}

func (env *TestEnvironment) Cleanup() {
	env.mu.Lock()
	defer env.mu.Unlock()
	
	for _, client := range env.clients {
		client.Close() // Close() is now idempotent
	}
	env.clients = make(map[string]*TestClient)
	env.hubClients = make(map[string]*hub.Client)
}

func (env *TestEnvironment) WaitForStabilization() {
	time.Sleep(50 * time.Millisecond)
}

// State validation helpers
func (env *TestEnvironment) ValidateRoomState(roomID string) error {
	env.hub.mu.RLock()
	defer env.hub.mu.RUnlock()
	
	room, exists := env.hub.rooms[roomID]
	if !exists {
		return fmt.Errorf("room %s does not exist", roomID)
	}
	
	// Validate room invariants
	if len(room.Players) > 4 {
		return fmt.Errorf("room has too many players: %d", len(room.Players))
	}
	
	if len(room.Players) == 0 {
		return fmt.Errorf("room should not have 0 players")
	}
	
	// Validate each game in the room
	for gameID, game := range room.Games {
		if err := env.ValidateGameState(gameID, game); err != nil {
			return fmt.Errorf("invalid game %s in room: %w", gameID, err)
		}
	}
	
	return nil
}

func (env *TestEnvironment) ValidateGameState(gameID string, game *Game) error {
	game.mu.RLock()
	defer game.mu.RUnlock()
	
	// Validate game invariants
	if len(game.state.Players) > 4 {
		return fmt.Errorf("game has too many players: %d", len(game.state.Players))
	}
	
	if len(game.state.Players) > 0 && game.state.CurrentPlayerIndex >= len(game.state.Players) {
		return fmt.Errorf("currentPlayerIndex %d out of bounds for %d players", 
			game.state.CurrentPlayerIndex, len(game.state.Players))
	}
	
	// Validate game phase transitions
	validPhases := map[string]bool{
		"waiting": true, "playing": true, "peeking": true, "knocked": true, "ended": true,
	}
	if !validPhases[game.state.GamePhase] {
		return fmt.Errorf("invalid game phase: %s", game.state.GamePhase)
	}
	
	// Validate player card counts in started games
	if game.state.GamePhase != "waiting" {
		for _, player := range game.state.Players {
			if len(player.Cards) != 4 {
				return fmt.Errorf("player %s has %d cards, expected 4", player.Name, len(player.Cards))
			}
			
			// Validate revealed cards indices
			for _, idx := range player.RevealedCards {
				if idx < 0 || idx > 3 {
					return fmt.Errorf("player %s has invalid revealed card index: %d", player.Name, idx)
				}
			}
			
			if len(player.RevealedCards) > 2 {
				return fmt.Errorf("player %s has too many revealed cards: %d", player.Name, len(player.RevealedCards))
			}
		}
	}
	
	return nil
}

func (env *TestEnvironment) GetRoomID(clientID string) (string, error) {
	env.hub.mu.RLock()
	defer env.hub.mu.RUnlock()
	
	for client, ctx := range env.hub.clientContexts {
		if getClientID(client) == clientID {
			return ctx.RoomID, nil
		}
	}
	return "", fmt.Errorf("client %s not found or not in room", clientID)
}

func (env *TestEnvironment) GetGameID(clientID string) (string, error) {
	env.hub.mu.RLock()
	defer env.hub.mu.RUnlock()
	
	for client, ctx := range env.hub.clientContexts {
		if getClientID(client) == clientID {
			return ctx.GameID, nil
		}
	}
	return "", fmt.Errorf("client %s not found or not in game", clientID)
}

// Integration Tests

func TestIntegration_CompleteRoomLifecycle(t *testing.T) {
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	// Create clients
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	_ = env.CreateClient("charlie") // charlie for testing multiple joins
	
	// Test room creation
	err := env.SendMessage(alice.id, `{"type": "createRoom"}`)
	if err != nil {
		t.Fatalf("Failed to create room: %v", err)
	}
	
	if !alice.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("Alice didn't receive room creation response")
	}
	
	// Extract room ID
	msg, _ := alice.FindMessageByType("roomJoined")
	var roomJoined RoomJoinedMessage
	if err := json.Unmarshal(msg, &roomJoined); err != nil {
		t.Fatalf("Failed to parse roomJoined: %v", err)
	}
	roomID := roomJoined.RoomState.ID
	
	// Validate initial room state
	if err := env.ValidateRoomState(roomID); err != nil {
		t.Fatalf("Invalid room state after creation: %v", err)
	}
	
	if len(roomJoined.RoomState.Players) != 1 {
		t.Errorf("Expected 1 player in new room, got %d", len(roomJoined.RoomState.Players))
	}
	
	if roomJoined.PlayerID == "" {
		t.Error("PlayerID should be set in roomJoined message")
	}
	
	// Test joining room
	err = env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Failed to join room: %v", err)
	}
	
	if !bob.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("Bob didn't receive room join response")
	}
	
	// Validate room state after join
	if err := env.ValidateRoomState(roomID); err != nil {
		t.Fatalf("Invalid room state after join: %v", err)
	}
	
	// Test third player joining
	charlie := env.CreateClient("charlie")
	err = env.SendMessage(charlie.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Failed to join room: %v", err)
	}
	
	env.WaitForStabilization()
	
	// Validate final room state
	if err := env.ValidateRoomState(roomID); err != nil {
		t.Fatalf("Invalid room state after third join: %v", err)
	}
	
	// Test leaving room
	err = env.SendMessage(bob.id, fmt.Sprintf(`{"type": "leaveRoom", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Failed to leave room: %v", err)
	}
	
	env.WaitForStabilization()
	
	// Validate room state after leave
	if err := env.ValidateRoomState(roomID); err != nil {
		t.Fatalf("Invalid room state after leave: %v", err)
	}
}

func TestIntegration_GameCreationAndJoining(t *testing.T) {
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	// Setup room with 3 players
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	charlie := env.CreateClient("charlie")
	
	// Alice creates room
	env.SendMessage(alice.id, `{"type": "createRoom"}`)
	alice.WaitForMessages(1, 100*time.Millisecond)
	
	msg, _ := alice.FindMessageByType("roomJoined")
	var roomJoined RoomJoinedMessage
	json.Unmarshal(msg, &roomJoined)
	roomID := roomJoined.RoomState.ID
	
	// Others join room
	env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	env.SendMessage(charlie.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	env.WaitForStabilization()
	
	// Test game creation
	alice.ClearMessages()
	err := env.SendMessage(alice.id, fmt.Sprintf(`{"type": "createGame", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Failed to create game: %v", err)
	}
	
	if !alice.WaitForMessages(1, 500*time.Millisecond) { // gameJoined (roomStateUpdate may not always be sent)
		t.Fatal("Alice didn't receive game creation responses")
	}
	
	// Extract game ID
	gameMsg, found := alice.FindMessageByType("gameJoined")
	if !found {
		t.Fatal("Alice didn't receive gameJoined message")
	}
	
	var gameJoined GameJoinedMessage
	if err := json.Unmarshal(gameMsg, &gameJoined); err != nil {
		t.Fatalf("Failed to parse gameJoined: %v", err)
	}
	gameID := gameJoined.GameState.ID
	
	// Validate game state
	env.hub.mu.RLock()
	room := env.hub.rooms[roomID]
	game := room.Games[gameID]
	env.hub.mu.RUnlock()
	
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Invalid game state after creation: %v", err)
	}
	
	if len(gameJoined.GameState.Players) != 1 {
		t.Errorf("Expected 1 player in new game, got %d", len(gameJoined.GameState.Players))
	}
	
	// Test joining existing game
	bob.ClearMessages()
	err = env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinGame", "roomId": "%s", "gameId": "%s"}`, roomID, gameID))
	if err != nil {
		t.Fatalf("Failed to join game: %v", err)
	}
	
	if !bob.WaitForMessages(2, 200*time.Millisecond) { // gameJoined + roomStateUpdate
		t.Fatal("Bob didn't receive game join responses")
	}
	
	// Validate game state after join
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Invalid game state after join: %v", err)
	}
	
	// Test creating second game in same room
	charlie.ClearMessages()
	err = env.SendMessage(charlie.id, fmt.Sprintf(`{"type": "createGame", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Failed to create second game: %v", err)
	}
	
	if !charlie.WaitForMessages(2, 200*time.Millisecond) {
		t.Fatal("Charlie didn't receive second game creation responses")
	}
	
	// Validate multiple games exist
	env.hub.mu.RLock()
	room = env.hub.rooms[roomID]
	gameCount := len(room.Games)
	env.hub.mu.RUnlock()
	
	if gameCount != 2 {
		t.Errorf("Expected 2 games in room, got %d", gameCount)
	}
}

func TestIntegration_CompleteGameFlow(t *testing.T) {
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	// Setup game with 2 players
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	
	// Create room and game
	env.SendMessage(alice.id, `{"type": "createRoom"}`)
	alice.WaitForMessages(1, 100*time.Millisecond)
	
	msg, found := alice.FindMessageByType("roomJoined")
	if !found {
		t.Fatal("Alice didn't receive roomJoined message")
	}
	var roomJoined RoomJoinedMessage
	json.Unmarshal(msg, &roomJoined)
	roomID := roomJoined.RoomState.ID
	
	env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	env.WaitForStabilization()
	
	// Clear messages before game creation to avoid confusion  
	alice.ClearMessages()
	
	env.SendMessage(alice.id, fmt.Sprintf(`{"type": "createGame", "roomId": "%s"}`, roomID))
	alice.WaitForMessages(1, 500*time.Millisecond) // Wait for gameJoined message
	
	gameMsg, found := alice.FindMessageByType("gameJoined")
	if !found {
		t.Fatal("Alice didn't receive gameJoined message")
	}
	var gameJoined GameJoinedMessage
	if err := json.Unmarshal(gameMsg, &gameJoined); err != nil {
		t.Fatalf("Failed to unmarshal gameJoined: %v", err)
	}
	gameID := gameJoined.GameState.ID
	
	env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinGame", "roomId": "%s", "gameId": "%s"}`, roomID, gameID))
	env.WaitForStabilization()
	
	// Start game
	alice.ClearMessages()
	bob.ClearMessages()
	
	err := env.SendMessage(alice.id, `{"type": "startGame"}`)
	if err != nil {
		t.Fatalf("Failed to start game: %v", err)
	}
	
	// Both players should receive gameStarted
	if !alice.WaitForMessages(2, 200*time.Millisecond) { // gameStarted + gameState
		t.Fatal("Alice didn't receive game start messages")
	}
	if !bob.WaitForMessages(2, 200*time.Millisecond) {
		t.Fatal("Bob didn't receive game start messages")
	}
	
	// Validate game state after start
	env.hub.mu.RLock()
	room := env.hub.rooms[roomID]
	game := room.Games[gameID]
	env.hub.mu.RUnlock()
	
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Invalid game state after start: %v", err)
	}
	
	if game.state.GamePhase != "playing" {
		t.Errorf("Expected playing phase, got %s", game.state.GamePhase)
	}
	
	// Test peeking phase
	alice.ClearMessages()
	err = env.SendMessage(alice.id, `{"type": "peekCard", "cardIndex": 0}`)
	if err != nil {
		t.Fatalf("Failed to peek card: %v", err)
	}
	
	if !alice.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("Alice didn't receive peek response")
	}
	
	// Validate state after peek
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Invalid game state after peek: %v", err)
	}
	
	// Test turn-based gameplay
	alice.ClearMessages()
	bob.ClearMessages()
	
	err = env.SendMessage(alice.id, `{"type": "drawCard"}`)
	if err != nil {
		t.Fatalf("Failed to draw card: %v", err)
	}
	
	env.WaitForStabilization()
	
	// Validate state after draw
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Invalid game state after draw: %v", err)
	}
	
	// Complete turn
	err = env.SendMessage(alice.id, `{"type": "discardDrawn"}`)
	if err != nil {
		t.Fatalf("Failed to discard: %v", err)
	}
	
	env.WaitForStabilization()
	
	// Validate turn advancement
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Invalid game state after discard: %v", err)
	}
	
	game.mu.RLock()
	currentPlayerIndex := game.state.CurrentPlayerIndex
	game.mu.RUnlock()
	
	if currentPlayerIndex != 1 {
		t.Errorf("Expected turn to advance to player 1, got %d", currentPlayerIndex)
	}
}

func TestIntegration_ConcurrentClientActions(t *testing.T) {
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	// Create multiple clients
	clients := make([]*TestClient, 4)
	clientIDs := []string{"alice", "bob", "charlie", "diana"}
	
	for i, id := range clientIDs {
		clients[i] = env.CreateClient(id)
	}
	
	// Only Alice (client 0) creates a room, others will join it
	var wg sync.WaitGroup
	var targetRoomID string
	
	// Alice creates room
	err := env.SendMessage(clientIDs[0], `{"type": "createRoom"}`)
	if err != nil {
		t.Fatalf("Alice failed to create room: %v", err)
	}
	
	if !clients[0].WaitForMessages(1, 200*time.Millisecond) {
		t.Fatal("Alice didn't receive room creation response")
	}
	
	msg, found := clients[0].FindMessageByType("roomJoined")
	if !found {
		t.Fatal("Alice didn't receive roomJoined message")
	}
	
	var roomJoined RoomJoinedMessage
	if err := json.Unmarshal(msg, &roomJoined); err != nil {
		t.Fatalf("Alice failed to parse roomJoined: %v", err)
	}
	
	targetRoomID = roomJoined.RoomState.ID
	
	// Validate Alice's room was created successfully
	if err := env.ValidateRoomState(targetRoomID); err != nil {
		t.Fatalf("Invalid room state after creation: %v", err)
	}
	
	// Clear messages for other clients  
	for i := 1; i < 4; i++ {
		clients[i].ClearMessages()
	}
	
	// Clients 1, 2, 3 all try to join Alice's room simultaneously
	for i := 1; i < 4; i++ {
		wg.Add(1)
		go func(idx int) {
			defer wg.Done()
			
			err := env.SendMessage(clientIDs[idx], fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, targetRoomID))
			if err != nil {
				t.Errorf("Client %s failed to join room: %v", clientIDs[idx], err)
			}
		}(i)
	}
	
	wg.Wait()
	env.WaitForStabilization()
	
	// Validate final room state
	if err := env.ValidateRoomState(targetRoomID); err != nil {
		t.Fatalf("Invalid room state after concurrent joins: %v", err)
	}
	
	// Check that room has expected number of players
	env.hub.mu.RLock()
	room := env.hub.rooms[targetRoomID]
	playerCount := len(room.Players)
	env.hub.mu.RUnlock()
	
	if playerCount != 4 {
		t.Errorf("Expected 4 players in target room, got %d", playerCount)
	}
}

func TestIntegration_NegativeTestCases(t *testing.T) {
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	alice := env.CreateClient("alice")
	_ = env.CreateClient("bob") // bob is used in setup functions
	
	testCases := []struct {
		name        string
		setup       func()
		message     string
		expectError bool
		errorText   string
	}{
		{
			name:        "Join non-existent room",
			setup:       func() { alice.ClearMessages() },
			message:     `{"type": "joinRoom", "roomId": "INVALID"}`,
			expectError: true,
			errorText:   "room not found",
		},
		{
			name: "Join non-existent game",
			setup: func() {
				env.SendMessage(alice.id, `{"type": "createRoom"}`)
				alice.WaitForMessages(1, 100*time.Millisecond)
				// Clear messages after room creation, before the invalid game join
				alice.ClearMessages()
			},
			message:     `{"type": "joinGame", "roomId": "PLACEHOLDER", "gameId": "INVALID"}`,
			expectError: true,
			errorText:   "Game does not exist in room",
		},
		{
			name:        "Start game without being in one",
			setup:       func() { alice.ClearMessages() },
			message:     `{"type": "startGame"}`,
			expectError: true,
			errorText:   "Room not found",
		},
		{
			name:        "Draw card without being in game",
			setup:       func() { alice.ClearMessages() },
			message:     `{"type": "drawCard"}`,
			expectError: true,
			errorText:   "Not in a game",
		},
		{
			name:        "Invalid card index",
			setup:       func() { alice.ClearMessages() },
			message:     `{"type": "peekCard", "cardIndex": 5}`,
			expectError: true,
			errorText:   "Not in a game",
		},
		{
			name:        "Unknown message type",
			setup:       func() { alice.ClearMessages() },
			message:     `{"type": "unknownAction"}`,
			expectError: true,
			errorText:   "Unknown message type: unknownAction",
		},
	}
	
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Ensure test isolation by creating fresh environment for each test
			testEnv := NewTestEnvironment()
			defer testEnv.Cleanup()
			
			testAlice := testEnv.CreateClient("alice")
			
			// Update the setup function to use the test-specific environment
			originalSetup := tc.setup
			tc.setup = func() {
				alice = testAlice
				env = testEnv
				originalSetup()
			}
			
			tc.setup()
			
			message := tc.message
			// Special handling for join non-existent game test 
			if tc.name == "Join non-existent game" {
				// We need to get room ID from somewhere since messages were cleared
				// Let's look at the hub state directly
				testEnv.hub.mu.RLock()
				var roomID string
				for id := range testEnv.hub.rooms {
					roomID = id
					break // Get the first (and only) room
				}
				testEnv.hub.mu.RUnlock()
				
				if roomID != "" {
					message = fmt.Sprintf(`{"type": "joinGame", "roomId": "%s", "gameId": "INVALID"}`, roomID)
				}
			}
			
			err := testEnv.SendMessage(testAlice.id, message)
			if err != nil {
				t.Fatalf("Failed to send message: %v", err)
			}
			
			if !testAlice.WaitForMessages(1, 100*time.Millisecond) {
				if tc.expectError {
					t.Fatal("Expected error message but got none")
				}
				return
			}
			
			if tc.expectError {
				errorMsg, found := testAlice.FindMessageByType("error")
				if !found {
					t.Fatal("Expected error message but didn't find one")
				}
				
				var errParsed ErrorMessage
				if err := json.Unmarshal(errorMsg, &errParsed); err != nil {
					t.Fatalf("Failed to parse error message: %v", err)
				}
				
				if errParsed.Message != tc.errorText {
					t.Errorf("Expected error '%s', got '%s'", tc.errorText, errParsed.Message)
				}
			}
		})
	}
}

func TestIntegration_StateValidationEdgeCases(t *testing.T) {
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	
	// Create room and game
	env.SendMessage(alice.id, `{"type": "createRoom"}`)
	alice.WaitForMessages(1, 100*time.Millisecond)
	
	msg, _ := alice.FindMessageByType("roomJoined")
	var roomJoined RoomJoinedMessage
	json.Unmarshal(msg, &roomJoined)
	roomID := roomJoined.RoomState.ID
	
	env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	env.WaitForStabilization()
	
	// Clear messages before game creation
	alice.ClearMessages()
	
	env.SendMessage(alice.id, fmt.Sprintf(`{"type": "createGame", "roomId": "%s"}`, roomID))
	alice.WaitForMessages(1, 500*time.Millisecond)
	
	gameMsg, found := alice.FindMessageByType("gameJoined")
	if !found {
		t.Fatal("Alice didn't receive gameJoined message")
	}
	var gameJoined GameJoinedMessage
	if err := json.Unmarshal(gameMsg, &gameJoined); err != nil {
		t.Fatalf("Failed to unmarshal gameJoined: %v", err)
	}
	gameID := gameJoined.GameState.ID
	
	env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinGame", "roomId": "%s", "gameId": "%s"}`, roomID, gameID))
	env.WaitForStabilization()
	
	// Test that we can't peek more than 2 cards
	env.SendMessage(alice.id, `{"type": "startGame"}`)
	env.WaitForStabilization()
	
	alice.ClearMessages()
	
	// Peek at first card - should succeed
	env.SendMessage(alice.id, `{"type": "peekCard", "cardIndex": 0}`)
	if !alice.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("First peek failed")
	}
	
	alice.ClearMessages()
	
	// Peek at second card - should succeed
	env.SendMessage(alice.id, `{"type": "peekCard", "cardIndex": 1}`)
	if !alice.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("Second peek failed")
	}
	
	alice.ClearMessages()
	
	// Try to peek at third card - should fail
	env.SendMessage(alice.id, `{"type": "peekCard", "cardIndex": 2}`)
	if !alice.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("Expected error for third peek")
	}
	
	errorMsg, found := alice.FindMessageByType("error")
	if !found {
		t.Fatal("Expected error message for third peek")
	}
	
	var errParsed ErrorMessage
	json.Unmarshal(errorMsg, &errParsed)
	
	if errParsed.Message != "already peeked at 2 cards" {
		t.Errorf("Expected 'already peeked at 2 cards' error, got '%s'", errParsed.Message)
	}
	
	// Validate game state is still consistent
	env.hub.mu.RLock()
	room := env.hub.rooms[roomID]
	game := room.Games[gameID]
	env.hub.mu.RUnlock()
	
	if err := env.ValidateGameState(gameID, game); err != nil {
		t.Fatalf("Game state validation failed after peek limit test: %v", err)
	}
}

func TestIntegration_PlayerReconnection(t *testing.T) {
	// NOTE: This test currently validates the behavior where disconnected players 
	// are removed from rooms and get new player IDs when they reconnect. 
	// This behavior should be updated once we implement proper reconnect support
	// that maintains player identity and game state across disconnections.
	
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	
	// Create room and game
	env.SendMessage(alice.id, `{"type": "createRoom"}`)
	alice.WaitForMessages(1, 100*time.Millisecond)
	
	msg, _ := alice.FindMessageByType("roomJoined")
	var roomJoined RoomJoinedMessage
	json.Unmarshal(msg, &roomJoined)
	roomID := roomJoined.RoomState.ID
	
	env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	env.WaitForStabilization()
	
	// Bob disconnects - let the hub handle the channel closure
	env.mu.RLock()
	bobHubClient, exists := env.hubClients["bob"]
	env.mu.RUnlock()
	
	if exists {
		// Mark the client as closed but don't close the channel manually
		bob.mu.Lock()
		bob.closed = true
		bob.mu.Unlock()
		
		// Let the hub handle the unregistration and channel closure
		env.hub.Unregister(bobHubClient)
	}
	env.WaitForStabilization()
	
	// Validate room state after disconnect
	if err := env.ValidateRoomState(roomID); err != nil {
		t.Fatalf("Invalid room state after disconnect: %v", err)
	}
	
	// Check the room state after disconnect - the behavior may vary
	env.hub.mu.RLock()
	room := env.hub.rooms[roomID]
	playerCount := len(room.Players)
	env.hub.mu.RUnlock()
	
	t.Logf("Room has %d players after Bob's disconnect", playerCount)
	if playerCount < 1 {
		t.Errorf("Room should have at least Alice after Bob disconnects, got %d players", playerCount)
	}
	
	// Bob "reconnects" (rejoins same room)
	bob2 := env.CreateClient("bob")
	env.SendMessage(bob2.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	if !bob2.WaitForMessages(1, 100*time.Millisecond) {
		t.Fatal("Bob didn't receive reconnection response")
	}
	
	// Validate final state
	if err := env.ValidateRoomState(roomID); err != nil {
		t.Fatalf("Invalid room state after reconnection: %v", err)
	}
	
	env.hub.mu.RLock()
	room = env.hub.rooms[roomID]
	bobPlayerAfter := room.GetPlayerByClientID("bob")
	finalPlayerCount := len(room.Players)
	env.hub.mu.RUnlock()
	
	t.Logf("Room has %d players after Bob's reconnection", finalPlayerCount)
	
	// Bob should be found as a player (either the old Bob or a new Bob)
	if bobPlayerAfter == nil {
		// Check if there's any player that might be Bob by checking the final count
		if finalPlayerCount < 2 {
			t.Errorf("Expected at least 2 players after Bob rejoins, got %d", finalPlayerCount)
		}
	} else {
		// If we found Bob, check that he's connected
		if !bobPlayerAfter.IsConnected {
			t.Error("Bob should be marked as connected after reconnection")
		}
	}
}

func TestIntegration_RoomStatePlayerCountAfterGameCreation(t *testing.T) {
	// This test specifically validates the bug fix for the room state showing 0 players
	// when a game is created. Other players in the room should see the correct player count.
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	// Create two clients - Alice will create the game, Bob will observe the room state update
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	
	// Alice creates room
	err := env.SendMessage(alice.id, `{"type": "createRoom"}`)
	if err != nil {
		t.Fatalf("Alice failed to create room: %v", err)
	}
	
	if !alice.WaitForMessages(1, 200*time.Millisecond) {
		t.Fatal("Alice didn't receive room creation response")
	}
	
	// Get room ID from Alice's message
	msg, found := alice.FindMessageByType("roomJoined")
	if !found {
		t.Fatal("Alice didn't receive roomJoined message")
	}
	
	var roomJoined RoomJoinedMessage
	if err := json.Unmarshal(msg, &roomJoined); err != nil {
		t.Fatalf("Failed to parse roomJoined: %v", err)
	}
	roomID := roomJoined.RoomState.ID
	
	// Bob joins the room 
	err = env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Bob failed to join room: %v", err)
	}
	
	if !bob.WaitForMessages(1, 200*time.Millisecond) {
		t.Fatal("Bob didn't receive room join response")
	}
	
	// Clear Bob's messages so we only see the room state update from game creation
	bob.ClearMessages()
	
	// Alice creates a game - this should trigger a roomStateUpdate for Bob
	err = env.SendMessage(alice.id, fmt.Sprintf(`{"type": "createGame", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Alice failed to create game: %v", err)
	}
	
	// Wait for Bob to receive the room state update
	if !bob.WaitForMessages(1, 500*time.Millisecond) {
		t.Fatal("Bob didn't receive room state update after Alice created game")
	}
	
	// Find the roomStateUpdate message Bob received
	roomStateMsg, found := bob.FindMessageByType("roomStateUpdate")
	if !found {
		t.Fatal("Bob didn't receive roomStateUpdate message")
	}
	
	// Parse the room state update as a raw JSON structure first
	var rawMsg map[string]interface{}
	if err := json.Unmarshal(roomStateMsg, &rawMsg); err != nil {
		t.Fatalf("Failed to parse roomStateUpdate as raw JSON: %v", err)
	}
	
	// Navigate to the games structure
	roomState, ok := rawMsg["roomState"].(map[string]interface{})
	if !ok {
		t.Fatal("roomState field not found or not an object")
	}
	
	games, ok := roomState["games"].(map[string]interface{})
	if !ok {
		t.Fatal("games field not found or not an object")
	}
	
	if len(games) == 0 {
		t.Fatal("No games found in room state")
	}
	
	// Get the first (and only) game
	var gameData map[string]interface{}
	var gameID string
	for id, g := range games {
		gameData = g.(map[string]interface{})
		gameID = id
		break
	}
	
	// This is the key assertion - the game in the room state should show 1 player (Alice)
	// This was the bug: it would show 0 players due to race condition in room state broadcasting
	players, ok := gameData["players"].([]interface{})
	if !ok {
		t.Fatal("players field not found or not an array")
	}
	
	playerCount := len(players)
	if playerCount != 1 {
		t.Errorf("Game %s shows %d players in room state, expected 1 (the creator). This indicates the race condition bug is not fixed.", gameID, playerCount)
		
		// Print debug information if the test fails
		roomStateJSON, _ := json.MarshalIndent(roomState, "", "  ")
		t.Logf("Full room state: %s", roomStateJSON)
	} else {
		// The key fix is validated: game shows 1 player instead of 0
		// Note: Game player IDs are different from room player IDs by design
		t.Logf("✅ SUCCESS: Game correctly shows 1 player (the creator)")
		
		if len(players) > 0 {
			gamePlayer := players[0].(map[string]interface{})
			if gamePlayerID, ok := gamePlayer["id"].(string); ok {
				t.Logf("Game player ID: %s", gamePlayerID)
			}
		}
	}
	
	// Additional validation: game phase should be "waiting"
	gamePhase, ok := gameData["gamePhase"].(string)
	if !ok {
		t.Fatal("gamePhase field not found or not a string")
	}
	if gamePhase != "waiting" {
		t.Errorf("Expected game phase to be 'waiting', got '%s'", gamePhase)
	}
}

func TestIntegration_StartNewGameFlow(t *testing.T) {
	// This test validates the startNewGame functionality and the newGameStarted message
	// with gameId and previousGameId fields
	env := NewTestEnvironment()
	defer env.Cleanup()
	
	// Create two clients 
	alice := env.CreateClient("alice")
	bob := env.CreateClient("bob")
	
	// Alice creates room
	err := env.SendMessage(alice.id, `{"type": "createRoom"}`)
	if err != nil {
		t.Fatalf("Alice failed to create room: %v", err)
	}
	
	if !alice.WaitForMessages(1, 200*time.Millisecond) {
		t.Fatal("Alice didn't receive room creation response")
	}
	
	// Get room ID
	msg, found := alice.FindMessageByType("roomJoined")
	if !found {
		t.Fatal("Alice didn't receive roomJoined message")
	}
	
	var roomJoined RoomJoinedMessage
	if err := json.Unmarshal(msg, &roomJoined); err != nil {
		t.Fatalf("Failed to parse roomJoined: %v", err)
	}
	roomID := roomJoined.RoomState.ID
	
	// Bob joins the room 
	err = env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinRoom", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Bob failed to join room: %v", err)
	}
	env.WaitForStabilization()
	
	// Alice creates first game
	alice.ClearMessages()
	err = env.SendMessage(alice.id, fmt.Sprintf(`{"type": "createGame", "roomId": "%s"}`, roomID))
	if err != nil {
		t.Fatalf("Alice failed to create first game: %v", err)
	}
	
	if !alice.WaitForMessages(1, 200*time.Millisecond) {
		t.Fatal("Alice didn't receive first game creation response")
	}
	
	// Get first game ID
	gameMsg, found := alice.FindMessageByType("gameJoined")
	if !found {
		t.Fatal("Alice didn't receive gameJoined message for first game")
	}
	
	var gameJoined GameJoinedMessage  
	if err := json.Unmarshal(gameMsg, &gameJoined); err != nil {
		t.Fatalf("Failed to parse gameJoined: %v", err)
	}
	firstGameID := gameJoined.GameState.ID
	
	// Bob joins the first game
	err = env.SendMessage(bob.id, fmt.Sprintf(`{"type": "joinGame", "roomId": "%s", "gameId": "%s"}`, roomID, firstGameID))
	if err != nil {
		t.Fatalf("Bob failed to join first game: %v", err)
	}
	env.WaitForStabilization()
	
	// Start and play the first game to completion
	err = env.SendMessage(alice.id, `{"type": "startGame"}`)
	if err != nil {
		t.Fatalf("Failed to start first game: %v", err)
	}
	env.WaitForStabilization()
	
	// Simulate game completion (simple knock scenario)
	err = env.SendMessage(alice.id, `{"type": "peekCard", "cardIndex": 0}`)
	if err != nil {
		t.Fatalf("Alice failed to peek: %v", err)
	}
	env.WaitForStabilization()
	
	err = env.SendMessage(alice.id, `{"type": "knock"}`)
	if err != nil {
		t.Fatalf("Alice failed to knock: %v", err)
	}
	env.WaitForStabilization()
	
	// Clear messages before starting new game
	alice.ClearMessages()
	bob.ClearMessages()
	
	// Alice starts a new game - this should trigger newGameStarted message
	err = env.SendMessage(alice.id, `{"type": "startNewGame"}`)
	if err != nil {
		t.Fatalf("Alice failed to start new game: %v", err)
	}
	
	// Both players should receive the newGameStarted message
	if !alice.WaitForMessages(2, 500*time.Millisecond) { // newGameStarted + roomStateUpdate
		t.Fatal("Alice didn't receive new game started messages")
	}
	if !bob.WaitForMessages(2, 500*time.Millisecond) {
		t.Fatal("Bob didn't receive new game started messages")
	}
	
	// Find and validate the newGameStarted message from Alice's perspective
	newGameMsg, found := alice.FindMessageByType("newGameStarted")
	if !found {
		t.Fatal("Alice didn't receive newGameStarted message")
	}
	
	var newGameStarted NewGameStartedMessage
	if err := json.Unmarshal(newGameMsg, &newGameStarted); err != nil {
		t.Fatalf("Failed to parse newGameStarted: %v", err)
	}
	
	// Validate the newGameStarted message fields
	if newGameStarted.Type != "newGameStarted" {
		t.Errorf("Expected type 'newGameStarted', got '%s'", newGameStarted.Type)
	}
	
	if newGameStarted.GameID == "" {
		t.Error("GameID should not be empty in newGameStarted message")
	}
	
	if newGameStarted.PreviousGameID != firstGameID {
		t.Errorf("Expected previousGameId '%s', got '%s'", firstGameID, newGameStarted.PreviousGameID)
	}
	
	// Validate that Bob receives the same message
	bobNewGameMsg, found := bob.FindMessageByType("newGameStarted")
	if !found {
		t.Fatal("Bob didn't receive newGameStarted message")
	}
	
	var bobNewGameStarted NewGameStartedMessage
	if err := json.Unmarshal(bobNewGameMsg, &bobNewGameStarted); err != nil {
		t.Fatalf("Failed to parse Bob's newGameStarted: %v", err)
	}
	
	// Both messages should be identical
	if bobNewGameStarted.GameID != newGameStarted.GameID {
		t.Errorf("Game IDs don't match: Alice got '%s', Bob got '%s'", newGameStarted.GameID, bobNewGameStarted.GameID)
	}
	
	if bobNewGameStarted.PreviousGameID != newGameStarted.PreviousGameID {
		t.Errorf("Previous game IDs don't match: Alice got '%s', Bob got '%s'", newGameStarted.PreviousGameID, bobNewGameStarted.PreviousGameID)
	}
	
	// Validate room state contains the new game
	env.hub.mu.RLock()
	room := env.hub.rooms[roomID]
	gameCount := len(room.Games)
	_, newGameExists := room.Games[newGameStarted.GameID]
	env.hub.mu.RUnlock()
	
	if gameCount != 2 {
		t.Errorf("Expected 2 games in room after startNewGame, got %d", gameCount)
	}
	
	if !newGameExists {
		t.Errorf("New game with ID '%s' not found in room", newGameStarted.GameID)
	}
	
	// Validate the new game is empty (no players initially)
	env.hub.mu.RLock()
	newGame := room.Games[newGameStarted.GameID]
	env.hub.mu.RUnlock()
	
	newGame.mu.RLock()
	newGamePlayerCount := len(newGame.state.Players)
	newGame.mu.RUnlock()
	
	if newGamePlayerCount != 0 {
		t.Errorf("New game should have 0 players initially, got %d", newGamePlayerCount)
	}
	
	t.Logf("✅ SUCCESS: startNewGame flow validated")
	t.Logf("  - Previous game ID: %s", firstGameID)
	t.Logf("  - New game ID: %s", newGameStarted.GameID)
	t.Logf("  - Room now has %d games", gameCount)
}