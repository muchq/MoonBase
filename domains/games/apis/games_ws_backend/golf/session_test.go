package golf

import (
	"testing"
	"time"
)

func TestGenerateSessionToken(t *testing.T) {
	token, err := GenerateSessionToken()
	if err != nil {
		t.Fatalf("Failed to generate token: %v", err)
	}

	if token == "" {
		t.Error("Generated token is empty")
	}

	// Token should be base64 encoded
	if err := ValidateSessionToken(token); err != nil {
		t.Errorf("Generated token failed validation: %v", err)
	}
}

func TestGenerateSessionTokenUniqueness(t *testing.T) {
	token1, err := GenerateSessionToken()
	if err != nil {
		t.Fatalf("Failed to generate token1: %v", err)
	}

	token2, err := GenerateSessionToken()
	if err != nil {
		t.Fatalf("Failed to generate token2: %v", err)
	}

	if token1 == token2 {
		t.Error("Generated tokens should be unique")
	}
}

func TestCreateSessionToken(t *testing.T) {
	st, err := CreateSessionToken()
	if err != nil {
		t.Fatalf("Failed to create session token: %v", err)
	}

	if st.Token == "" {
		t.Error("Session token string is empty")
	}

	if st.CreatedAt.IsZero() {
		t.Error("CreatedAt should be set")
	}

	if st.ExpiresAt.IsZero() {
		t.Error("ExpiresAt should be set")
	}

	expectedExpiry := st.CreatedAt.Add(SessionTokenLifetime)
	if !st.ExpiresAt.Equal(expectedExpiry) {
		t.Errorf("ExpiresAt = %v, want %v", st.ExpiresAt, expectedExpiry)
	}
}

func TestSessionTokenIsExpired(t *testing.T) {
	tests := []struct {
		name      string
		expiresAt time.Time
		want      bool
	}{
		{
			name:      "token expired 1 hour ago",
			expiresAt: time.Now().Add(-1 * time.Hour),
			want:      true,
		},
		{
			name:      "token expires in 1 hour",
			expiresAt: time.Now().Add(1 * time.Hour),
			want:      false,
		},
		{
			name:      "token expires in 1 second",
			expiresAt: time.Now().Add(1 * time.Second),
			want:      false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			st := &SessionToken{
				Token:     "test-token",
				CreatedAt: time.Now(),
				ExpiresAt: tt.expiresAt,
			}

			if got := st.IsExpired(); got != tt.want {
				t.Errorf("IsExpired() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestSessionTokenIsValid(t *testing.T) {
	tests := []struct {
		name      string
		expiresAt time.Time
		want      bool
	}{
		{
			name:      "expired token is invalid",
			expiresAt: time.Now().Add(-1 * time.Hour),
			want:      false,
		},
		{
			name:      "future token is valid",
			expiresAt: time.Now().Add(1 * time.Hour),
			want:      true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			st := &SessionToken{
				Token:     "test-token",
				CreatedAt: time.Now(),
				ExpiresAt: tt.expiresAt,
			}

			if got := st.IsValid(); got != tt.want {
				t.Errorf("IsValid() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestValidateSessionToken(t *testing.T) {
	tests := []struct {
		name    string
		token   string
		wantErr bool
	}{
		{
			name:    "empty token",
			token:   "",
			wantErr: true,
		},
		{
			name:    "invalid base64",
			token:   "not-valid-base64!@#$%",
			wantErr: true,
		},
		{
			name:    "valid base64 token",
			token:   "dGVzdC10b2tlbi1kYXRh", // "test-token-data" in base64
			wantErr: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidateSessionToken(tt.token)
			if (err != nil) != tt.wantErr {
				t.Errorf("ValidateSessionToken() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func TestValidateRealGeneratedToken(t *testing.T) {
	token, err := GenerateSessionToken()
	if err != nil {
		t.Fatalf("Failed to generate token: %v", err)
	}

	if err := ValidateSessionToken(token); err != nil {
		t.Errorf("Real generated token failed validation: %v", err)
	}
}
