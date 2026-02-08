package golf

import (
	"encoding/json"
	"testing"
	"time"

	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/hub"
	"github.com/muchq/moonbase/domains/games/apis/games_ws_backend/players"
)

// TestAuthenticationNewSession tests the authentication flow for a new session
func TestAuthenticationNewSession(t *testing.T) {
	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	client := createMockClient()
	h.Register(client)

	// Wait for registration
	time.Sleep(10 * time.Millisecond)

	// Verify client is not authenticated
	h.mu.RLock()
	authenticated := h.authenticatedClients[client]
	h.mu.RUnlock()

	if authenticated {
		t.Error("Client should not be authenticated before sending authenticate message")
	}

	// Send authenticate message (no session token = new session)
	authMsg := AuthenticateMessage{
		Type:         "authenticate",
		SessionToken: "",
	}
	sendMessage(t, h, client, authMsg)

	// Wait for processing
	time.Sleep(10 * time.Millisecond)

	// Verify client is now authenticated
	h.mu.RLock()
	authenticated = h.authenticatedClients[client]
	ctx := h.clientContexts[client]
	h.mu.RUnlock()

	if !authenticated {
		t.Error("Client should be authenticated after sending authenticate message")
	}

	if ctx == nil {
		t.Fatal("Client context should exist after authentication")
	}

	if ctx.SessionToken == "" {
		t.Error("Session token should be generated")
	}

	if ctx.PlayerID == "" {
		t.Error("Player ID should be generated")
	}

	// Verify we received an authenticated message
	select {
	case msg := <-client.Send:
		var authResponse AuthenticatedMessage
		if err := json.Unmarshal(msg, &authResponse); err != nil {
			t.Fatalf("Failed to unmarshal authenticated message: %v", err)
		}

		if authResponse.Type != "authenticated" {
			t.Errorf("Expected 'authenticated' message, got %s", authResponse.Type)
		}

		if authResponse.SessionToken == "" {
			t.Error("Authenticated message should include session token")
		}

		if authResponse.Reconnected {
			t.Error("New session should not be marked as reconnected")
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("Timeout waiting for authenticated message")
	}
}

// TestAuthenticationReconnect tests reconnecting with an existing session token
func TestAuthenticationReconnect(t *testing.T) {
	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	// Create first session
	client1 := createMockClient()
	h.Register(client1)
	time.Sleep(10 * time.Millisecond)

	// Authenticate
	sendMessage(t, h, client1, AuthenticateMessage{Type: "authenticate"})
	time.Sleep(10 * time.Millisecond)

	// Get session token
	h.mu.RLock()
	sessionToken := h.clientContexts[client1].SessionToken
	playerID := h.clientContexts[client1].PlayerID
	h.mu.RUnlock()

	// Disconnect client1
	h.Unregister(client1)
	time.Sleep(10 * time.Millisecond)

	// Create second client and reconnect with same token
	client2 := createMockClient()
	h.Register(client2)
	time.Sleep(10 * time.Millisecond)

	// Reconnect with session token
	sendMessage(t, h, client2, AuthenticateMessage{
		Type:         "authenticate",
		SessionToken: sessionToken,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify client2 has the same context
	h.mu.RLock()
	ctx := h.clientContexts[client2]
	authenticated := h.authenticatedClients[client2]
	h.mu.RUnlock()

	if !authenticated {
		t.Error("Reconnected client should be authenticated")
	}

	if ctx == nil {
		t.Fatal("Reconnected client should have context")
	}

	if ctx.SessionToken != sessionToken {
		t.Errorf("Session token mismatch: got %s, want %s", ctx.SessionToken, sessionToken)
	}

	if ctx.PlayerID != playerID {
		t.Errorf("Player ID mismatch: got %s, want %s", ctx.PlayerID, playerID)
	}

	// Verify authenticated message indicates reconnection
	select {
	case msg := <-client2.Send:
		var authResponse AuthenticatedMessage
		if err := json.Unmarshal(msg, &authResponse); err != nil {
			t.Fatalf("Failed to unmarshal authenticated message: %v", err)
		}

		if !authResponse.Reconnected {
			t.Error("Reconnected session should be marked as reconnected")
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("Timeout waiting for authenticated message")
	}
}

// TestMessageRequiresAuthentication tests that messages require authentication
func TestMessageRequiresAuthentication(t *testing.T) {
	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	client := createMockClient()
	h.Register(client)
	time.Sleep(10 * time.Millisecond)

	// Try to send a game message without authenticating
	createRoomMsg := CreateRoomMessage{Type: "createRoom"}
	sendMessage(t, h, client, createRoomMsg)
	time.Sleep(10 * time.Millisecond)

	// Should receive an error
	select {
	case msg := <-client.Send:
		var errMsg ErrorMessage
		if err := json.Unmarshal(msg, &errMsg); err != nil {
			t.Fatalf("Failed to unmarshal error message: %v", err)
		}

		if errMsg.Type != "error" {
			t.Errorf("Expected error message, got %s", errMsg.Type)
		}

		if errMsg.Message == "" {
			t.Error("Error message should have content")
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("Timeout waiting for error message")
	}
}

// TestReconnectWithinGracePeriod tests reconnecting before cleanup
func TestReconnectWithinGracePeriod(t *testing.T) {
	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	// Create and authenticate client
	client1 := createMockClient()
	h.Register(client1)
	time.Sleep(10 * time.Millisecond)

	sendMessage(t, h, client1, AuthenticateMessage{Type: "authenticate"})
	time.Sleep(10 * time.Millisecond)

	// Drain authenticated message
	<-client1.Send

	// Create room
	sendMessage(t, h, client1, CreateRoomMessage{Type: "createRoom"})
	time.Sleep(10 * time.Millisecond)

	// Drain room joined message
	<-client1.Send

	// Get session token and room ID
	h.mu.RLock()
	sessionToken := h.clientContexts[client1].SessionToken
	roomID := h.clientContexts[client1].RoomID
	h.mu.RUnlock()

	// Disconnect
	h.Unregister(client1)
	time.Sleep(10 * time.Millisecond)

	// Verify cleanup timer was created
	h.mu.RLock()
	_, timerExists := h.cleanupTimers[sessionToken]
	h.mu.RUnlock()

	if !timerExists {
		t.Error("Cleanup timer should be created on disconnect")
	}

	// Reconnect within grace period
	client2 := createMockClient()
	h.Register(client2)
	time.Sleep(10 * time.Millisecond)

	sendMessage(t, h, client2, AuthenticateMessage{
		Type:         "authenticate",
		SessionToken: sessionToken,
	})
	time.Sleep(10 * time.Millisecond)

	// Verify reconnection
	h.mu.RLock()
	ctx := h.clientContexts[client2]
	_, timerStillExists := h.cleanupTimers[sessionToken]
	room := h.rooms[roomID]
	h.mu.RUnlock()

	if timerStillExists {
		t.Error("Cleanup timer should be cancelled on reconnect")
	}

	if ctx == nil {
		t.Fatal("Context should exist after reconnect")
	}

	if ctx.RoomID != roomID {
		t.Errorf("Room ID should be preserved: got %s, want %s", ctx.RoomID, roomID)
	}

	// Verify player is marked as connected
	if room != nil {
		player := room.GetPlayerByClientID(ctx.PlayerID)
		if player == nil {
			t.Fatal("Player should still be in room")
		}

		if !player.IsConnected {
			t.Error("Player should be marked as connected after reconnect")
		}

		if player.DisconnectedAt != nil {
			t.Error("DisconnectedAt should be cleared on reconnect")
		}
	} else {
		t.Error("Room should still exist after reconnect")
	}
}

// TestCleanupAfterGracePeriod tests cleanup after grace period expires
func TestCleanupAfterGracePeriod(t *testing.T) {
	// This test needs a shorter grace period for testing
	// We'll directly test the cleanup function

	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	// Create and authenticate client
	client := createMockClient()
	h.Register(client)
	time.Sleep(10 * time.Millisecond)

	sendMessage(t, h, client, AuthenticateMessage{Type: "authenticate"})
	time.Sleep(10 * time.Millisecond)
	<-client.Send // Drain authenticated message

	// Create room
	sendMessage(t, h, client, CreateRoomMessage{Type: "createRoom"})
	time.Sleep(10 * time.Millisecond)
	<-client.Send // Drain room joined message

	// Get session token
	h.mu.RLock()
	sessionToken := h.clientContexts[client].SessionToken
	roomID := h.clientContexts[client].RoomID
	h.mu.RUnlock()

	// Disconnect
	h.Unregister(client)
	time.Sleep(10 * time.Millisecond)

	// Manually trigger cleanup (instead of waiting for timer)
	h.cleanupDisconnectedPlayer(sessionToken)

	// Verify session token was removed
	h.mu.RLock()
	_, tokenExists := h.sessionTokens[sessionToken]
	_, timerExists := h.cleanupTimers[sessionToken]
	room := h.rooms[roomID]
	h.mu.RUnlock()

	if tokenExists {
		t.Error("Session token should be removed after cleanup")
	}

	if timerExists {
		t.Error("Cleanup timer should be removed after cleanup")
	}

	if room != nil && len(room.Players) > 0 {
		t.Error("Player should be removed from room after cleanup")
	}
}

// TestInvalidSessionToken tests authentication with invalid token
func TestInvalidSessionToken(t *testing.T) {
	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	client := createMockClient()
	h.Register(client)
	time.Sleep(10 * time.Millisecond)

	// Try to authenticate with invalid token
	sendMessage(t, h, client, AuthenticateMessage{
		Type:         "authenticate",
		SessionToken: "invalid-token-!@#$",
	})
	time.Sleep(10 * time.Millisecond)

	// Should receive error
	select {
	case msg := <-client.Send:
		var errMsg ErrorMessage
		if err := json.Unmarshal(msg, &errMsg); err != nil {
			t.Fatalf("Failed to unmarshal error message: %v", err)
		}

		if errMsg.Type != "error" {
			t.Errorf("Expected error message, got %s", errMsg.Type)
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("Timeout waiting for error message")
	}

	// Verify client is not authenticated
	h.mu.RLock()
	authenticated := h.authenticatedClients[client]
	h.mu.RUnlock()

	if authenticated {
		t.Error("Client should not be authenticated with invalid token")
	}
}

// TestNonexistentSessionToken tests authentication with nonexistent token
func TestNonexistentSessionToken(t *testing.T) {
	h := NewGolfHub(&players.DeterministicIDGenerator{}).(*GolfHub)
	go h.Run()

	client := createMockClient()
	h.Register(client)
	time.Sleep(10 * time.Millisecond)

	// Generate a valid-looking but nonexistent token
	validToken, _ := GenerateSessionToken()

	sendMessage(t, h, client, AuthenticateMessage{
		Type:         "authenticate",
		SessionToken: validToken,
	})
	time.Sleep(10 * time.Millisecond)

	// Should receive error
	select {
	case msg := <-client.Send:
		var errMsg ErrorMessage
		if err := json.Unmarshal(msg, &errMsg); err != nil {
			t.Fatalf("Failed to unmarshal error message: %v", err)
		}

		if errMsg.Type != "error" {
			t.Errorf("Expected error message, got %s", errMsg.Type)
		}
	case <-time.After(100 * time.Millisecond):
		t.Error("Timeout waiting for error message")
	}
}

// Helper function to create a mock client
func createMockClient() *hub.Client {
	return &hub.Client{
		Send: make(chan []byte, 256),
	}
}

// Helper function to send a message from client to hub
func sendMessage(t *testing.T, h *GolfHub, client *hub.Client, msg interface{}) {
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatalf("Failed to marshal message: %v", err)
	}

	h.GameMessage(hub.GameMessageData{
		Message: data,
		Sender:  client,
	})
}
