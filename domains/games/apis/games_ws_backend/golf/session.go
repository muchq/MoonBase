package golf

import (
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"time"
)

// SessionToken represents a session token with metadata
type SessionToken struct {
	Token     string
	CreatedAt time.Time
	ExpiresAt time.Time
}

// GenerateSessionToken generates a cryptographically secure session token
func GenerateSessionToken() (string, error) {
	// Generate 32 bytes (256 bits) of random data
	tokenBytes := make([]byte, 32)
	_, err := rand.Read(tokenBytes)
	if err != nil {
		return "", fmt.Errorf("failed to generate random token: %w", err)
	}

	// Encode as URL-safe base64
	token := base64.URLEncoding.EncodeToString(tokenBytes)
	return token, nil
}

// CreateSessionToken creates a new session token with expiry
func CreateSessionToken() (*SessionToken, error) {
	token, err := GenerateSessionToken()
	if err != nil {
		return nil, err
	}

	now := time.Now()
	return &SessionToken{
		Token:     token,
		CreatedAt: now,
		ExpiresAt: now.Add(SessionTokenLifetime),
	}, nil
}

// IsExpired checks if a session token has expired
func (st *SessionToken) IsExpired() bool {
	return time.Now().After(st.ExpiresAt)
}

// IsValid checks if a session token is valid (not expired)
func (st *SessionToken) IsValid() bool {
	return !st.IsExpired()
}

// ValidateSessionToken checks if a token string is properly formatted
func ValidateSessionToken(token string) error {
	if token == "" {
		return fmt.Errorf("token is empty")
	}

	// Verify it's valid base64
	_, err := base64.URLEncoding.DecodeString(token)
	if err != nil {
		return fmt.Errorf("invalid token format: %w", err)
	}

	return nil
}
