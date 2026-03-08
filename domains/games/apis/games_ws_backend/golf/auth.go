package golf

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"strings"
	"time"
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

type jwtHeader struct {
	Alg string `json:"alg"`
	Typ string `json:"typ"`
}

type jwtClaims struct {
	Sub string `json:"sub"` // playerID
	Exp int64  `json:"exp"` // expiry unix timestamp
	Iat int64  `json:"iat"` // issued at unix timestamp
}

// CreateToken generates a JWT containing the playerID with the given TTL.
func (tm *TokenManager) CreateToken(playerID string, ttl time.Duration) (string, error) {
	header := jwtHeader{Alg: "HS256", Typ: "JWT"}
	claims := jwtClaims{
		Sub: playerID,
		Exp: time.Now().Add(ttl).Unix(),
		Iat: time.Now().Unix(),
	}

	headerJSON, err := json.Marshal(header)
	if err != nil {
		return "", fmt.Errorf("failed to marshal header: %w", err)
	}

	claimsJSON, err := json.Marshal(claims)
	if err != nil {
		return "", fmt.Errorf("failed to marshal claims: %w", err)
	}

	headerB64 := base64.RawURLEncoding.EncodeToString(headerJSON)
	claimsB64 := base64.RawURLEncoding.EncodeToString(claimsJSON)

	signingInput := headerB64 + "." + claimsB64
	signature := tm.sign([]byte(signingInput))
	signatureB64 := base64.RawURLEncoding.EncodeToString(signature)

	return signingInput + "." + signatureB64, nil
}

// ValidateToken verifies the JWT signature, checks expiry, and returns the playerID.
// Strictly validates that the algorithm is HS256 to prevent algorithm confusion attacks.
func (tm *TokenManager) ValidateToken(token string) (string, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) != 3 {
		return "", fmt.Errorf("invalid token format")
	}

	// Decode and validate header - enforce HS256 algorithm
	headerJSON, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return "", fmt.Errorf("invalid header encoding: %w", err)
	}

	var header jwtHeader
	if err := json.Unmarshal(headerJSON, &header); err != nil {
		return "", fmt.Errorf("invalid header: %w", err)
	}

	if header.Alg != "HS256" {
		return "", fmt.Errorf("unsupported algorithm: %s (only HS256 is accepted)", header.Alg)
	}

	// Verify signature
	signingInput := parts[0] + "." + parts[1]
	signature, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return "", fmt.Errorf("invalid signature encoding: %w", err)
	}

	expectedSig := tm.sign([]byte(signingInput))
	if !hmac.Equal(signature, expectedSig) {
		return "", fmt.Errorf("invalid signature")
	}

	// Decode claims
	claimsJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return "", fmt.Errorf("invalid claims encoding: %w", err)
	}

	var claims jwtClaims
	if err := json.Unmarshal(claimsJSON, &claims); err != nil {
		return "", fmt.Errorf("invalid claims: %w", err)
	}

	if time.Now().Unix() > claims.Exp {
		return "", fmt.Errorf("token expired")
	}

	if claims.Sub == "" {
		return "", fmt.Errorf("token missing subject")
	}

	return claims.Sub, nil
}

func (tm *TokenManager) sign(data []byte) []byte {
	mac := hmac.New(sha256.New, tm.secret)
	mac.Write(data)
	return mac.Sum(nil)
}
