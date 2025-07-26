package main

import (
	"encoding/json"
	"testing"
)

func TestParseGameMessage(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		wantErr bool
		msgType string
	}{
		{
			name:    "valid player_join message",
			input:   `{"type":"player_join","playerId":"player-123","position":[10.0,0,-5.0],"color":[0.8,0.2,0.6],"timestamp":1703123456789}`,
			wantErr: false,
			msgType: "player_join",
		},
		{
			name:    "valid position_update message",
			input:   `{"type":"position_update","playerId":"player-123","position":[15.0,0,-8.0],"timestamp":1703123456890}`,
			wantErr: false,
			msgType: "position_update",
		},
		{
			name:    "valid player_leave message",
			input:   `{"type":"player_leave","playerId":"player-123","timestamp":1703123456999}`,
			wantErr: false,
			msgType: "player_leave",
		},
		{
			name:    "invalid JSON",
			input:   `{"type":"player_join","playerId":}`,
			wantErr: true,
		},
		{
			name:    "empty message",
			input:   ``,
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			msg, err := ParseGameMessage([]byte(tt.input))
			if (err != nil) != tt.wantErr {
				t.Errorf("ParseGameMessage() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if !tt.wantErr && msg.Type != tt.msgType {
				t.Errorf("ParseGameMessage() type = %v, want %v", msg.Type, tt.msgType)
			}
		})
	}
}

func TestValidatePosition(t *testing.T) {
	tests := []struct {
		name    string
		pos     Position
		wantErr bool
	}{
		{
			name:    "valid position",
			pos:     Position{10.0, 0, -5.0},
			wantErr: false,
		},
		{
			name:    "valid boundary position",
			pos:     Position{50.0, 0, -50.0},
			wantErr: false,
		},
		{
			name:    "invalid x position over boundary",
			pos:     Position{51.0, 0, 0},
			wantErr: true,
		},
		{
			name:    "invalid x position under boundary",
			pos:     Position{-51.0, 0, 0},
			wantErr: true,
		},
		{
			name:    "invalid z position over boundary",
			pos:     Position{0, 0, 51.0},
			wantErr: true,
		},
		{
			name:    "invalid z position under boundary",
			pos:     Position{0, 0, -51.0},
			wantErr: true,
		},
		{
			name:    "invalid y position not zero",
			pos:     Position{0, 5.0, 0},
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidatePosition(tt.pos)
			if (err != nil) != tt.wantErr {
				t.Errorf("ValidatePosition() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func TestValidateColor(t *testing.T) {
	tests := []struct {
		name    string
		color   Color
		wantErr bool
	}{
		{
			name:    "valid color",
			color:   Color{0.8, 0.2, 0.6},
			wantErr: false,
		},
		{
			name:    "valid boundary color",
			color:   Color{0.0, 1.0, 0.5},
			wantErr: false,
		},
		{
			name:    "invalid color over 1.0",
			color:   Color{1.1, 0.5, 0.5},
			wantErr: true,
		},
		{
			name:    "invalid color under 0.0",
			color:   Color{-0.1, 0.5, 0.5},
			wantErr: true,
		},
		{
			name:    "invalid green over 1.0",
			color:   Color{0.5, 1.1, 0.5},
			wantErr: true,
		},
		{
			name:    "invalid blue under 0.0",
			color:   Color{0.5, 0.5, -0.1},
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidateColor(tt.color)
			if (err != nil) != tt.wantErr {
				t.Errorf("ValidateColor() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func TestCreatePlayerJoinMessage(t *testing.T) {
	player := &Player{
		ID:       "player-test123",
		Position: Position{10.0, 0, -5.0},
		Color:    Color{0.8, 0.2, 0.6},
	}

	data, err := CreatePlayerJoinMessage(player)
	if err != nil {
		t.Fatalf("CreatePlayerJoinMessage() error = %v", err)
	}

	var msg PlayerJoinMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		t.Fatalf("Failed to unmarshal message: %v", err)
	}

	if msg.Type != "player_join" {
		t.Errorf("Expected type 'player_join', got %s", msg.Type)
	}
	if msg.PlayerID != player.ID {
		t.Errorf("Expected playerID %s, got %s", player.ID, msg.PlayerID)
	}
	if msg.Position != player.Position {
		t.Errorf("Expected position %v, got %v", player.Position, msg.Position)
	}
	if msg.Color != player.Color {
		t.Errorf("Expected color %v, got %v", player.Color, msg.Color)
	}
	if msg.Timestamp == 0 {
		t.Error("Expected non-zero timestamp")
	}
}

func TestCreatePositionUpdateMessage(t *testing.T) {
	playerID := "player-test123"
	position := Position{15.0, 0, -8.0}

	data, err := CreatePositionUpdateMessage(playerID, position)
	if err != nil {
		t.Fatalf("CreatePositionUpdateMessage() error = %v", err)
	}

	var msg PositionUpdateMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		t.Fatalf("Failed to unmarshal message: %v", err)
	}

	if msg.Type != "position_update" {
		t.Errorf("Expected type 'position_update', got %s", msg.Type)
	}
	if msg.PlayerID != playerID {
		t.Errorf("Expected playerID %s, got %s", playerID, msg.PlayerID)
	}
	if msg.Position != position {
		t.Errorf("Expected position %v, got %v", position, msg.Position)
	}
	if msg.Timestamp == 0 {
		t.Error("Expected non-zero timestamp")
	}
}

func TestCreatePlayerLeaveMessage(t *testing.T) {
	playerID := "player-test123"

	data, err := CreatePlayerLeaveMessage(playerID)
	if err != nil {
		t.Fatalf("CreatePlayerLeaveMessage() error = %v", err)
	}

	var msg PlayerLeaveMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		t.Fatalf("Failed to unmarshal message: %v", err)
	}

	if msg.Type != "player_leave" {
		t.Errorf("Expected type 'player_leave', got %s", msg.Type)
	}
	if msg.PlayerID != playerID {
		t.Errorf("Expected playerID %s, got %s", playerID, msg.PlayerID)
	}
	if msg.Timestamp == 0 {
		t.Error("Expected non-zero timestamp")
	}
}

func TestCreateGameStateMessage(t *testing.T) {
	players := map[string]*Player{
		"player-1": {
			ID:       "player-1",
			Position: Position{10.0, 0, -5.0},
			Color:    Color{0.8, 0.2, 0.6},
		},
		"player-2": {
			ID:       "player-2",
			Position: Position{20.0, 0, 15.0},
			Color:    Color{0.3, 0.9, 0.4},
		},
	}

	data, err := CreateGameStateMessage(players)
	if err != nil {
		t.Fatalf("CreateGameStateMessage() error = %v", err)
	}

	var msg GameStateMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		t.Fatalf("Failed to unmarshal message: %v", err)
	}

	if msg.Type != "game_state" {
		t.Errorf("Expected type 'game_state', got %s", msg.Type)
	}
	if len(msg.Players) != 2 {
		t.Errorf("Expected 2 players, got %d", len(msg.Players))
	}
	if msg.Timestamp == 0 {
		t.Error("Expected non-zero timestamp")
	}

	// Check that all players are included
	playerIDs := make(map[string]bool)
	for _, player := range msg.Players {
		playerIDs[player.PlayerID] = true
	}
	if !playerIDs["player-1"] || !playerIDs["player-2"] {
		t.Error("Not all players included in game state")
	}
}

func TestGameMessage_JSONRoundTrip(t *testing.T) {
	// Test that we can parse our own generated messages
	player := &Player{
		ID:       "player-roundtrip",
		Position: Position{25.5, 0, -12.3},
		Color:    Color{0.7, 0.1, 0.9},
	}

	// Test player join message round trip
	joinData, _ := CreatePlayerJoinMessage(player)
	parsedJoin, err := ParseGameMessage(joinData)
	if err != nil {
		t.Fatalf("Failed to parse generated join message: %v", err)
	}
	if parsedJoin.Type != "player_join" || parsedJoin.PlayerID != player.ID {
		t.Error("Join message round trip failed")
	}

	// Test position update message round trip
	updateData, _ := CreatePositionUpdateMessage(player.ID, player.Position)
	parsedUpdate, err := ParseGameMessage(updateData)
	if err != nil {
		t.Fatalf("Failed to parse generated update message: %v", err)
	}
	if parsedUpdate.Type != "position_update" || parsedUpdate.PlayerID != player.ID {
		t.Error("Update message round trip failed")
	}

	// Test player leave message round trip
	leaveData, _ := CreatePlayerLeaveMessage(player.ID)
	parsedLeave, err := ParseGameMessage(leaveData)
	if err != nil {
		t.Fatalf("Failed to parse generated leave message: %v", err)
	}
	if parsedLeave.Type != "player_leave" || parsedLeave.PlayerID != player.ID {
		t.Error("Leave message round trip failed")
	}
}