package golf

import (
	"crypto/rand"
	"fmt"
	"time"

	"github.com/golang-jwt/jwt/v5"
)

// TokenManager handles JWT creation and validation for player authentication.
// Uses HMAC-SHA256 with a random secret generated at startup.
type TokenManager struct {
	secret []byte
}

// NewTokenManager creates a TokenManager with a cryptographically random 32-byte secret.
func NewTokenManager() *TokenManager {
	secret := make([]byte, 32)
	if _, err := rand.Read(secret); err != nil {
		panic("failed to generate JWT secret: " + err.Error())
	}
	return &TokenManager{secret: secret}
}

// NewTokenManagerWithSecret creates a TokenManager with a provided secret (for testing).
func NewTokenManagerWithSecret(secret []byte) *TokenManager {
	return &TokenManager{secret: secret}
}

// CreateToken generates a JWT containing the playerID with the given TTL.
func (tm *TokenManager) CreateToken(playerID string, ttl time.Duration) (string, error) {
	claims := jwt.RegisteredClaims{
		Subject:   playerID,
		ExpiresAt: jwt.NewNumericDate(time.Now().Add(ttl)),
		IssuedAt:  jwt.NewNumericDate(time.Now()),
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString(tm.secret)
}

// ValidateToken verifies the JWT signature, checks expiry, and returns the playerID.
// Strictly validates that the signing method is HMAC to prevent algorithm confusion attacks.
func (tm *TokenManager) ValidateToken(tokenString string) (string, error) {
	token, err := jwt.ParseWithClaims(tokenString, &jwt.RegisteredClaims{}, func(token *jwt.Token) (interface{}, error) {
		if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
			return nil, fmt.Errorf("unsupported signing method: %v", token.Header["alg"])
		}
		return tm.secret, nil
	})
	if err != nil {
		return "", fmt.Errorf("invalid token: %w", err)
	}

	claims, ok := token.Claims.(*jwt.RegisteredClaims)
	if !ok {
		return "", fmt.Errorf("invalid token claims")
	}

	if claims.Subject == "" {
		return "", fmt.Errorf("token missing subject")
	}

	return claims.Subject, nil
}
