package com.muchq.mcpclient.oauth;

import java.time.Instant;

/**
 * Thread-safe manager for OAuth access and refresh tokens.
 *
 * Responsibilities:
 * - Store access token, refresh token, and expiration time
 * - Check token expiration before returning
 * - Thread-safe access for concurrent requests
 *
 * Note: This is an in-memory implementation suitable for demo purposes.
 * Production implementations should use secure storage (OS keychain, encrypted file, etc.)
 */
public class TokenManager {

    /**
     * Safety buffer in seconds before token expiration.
     * This prevents race conditions where a token expires between validation and use.
     */
    private static final long EXPIRATION_BUFFER_SECONDS = 30;

    private volatile String accessToken;
    private volatile String refreshToken;
    private volatile Instant expiresAt;

    /**
     * Stores OAuth tokens with expiration tracking.
     *
     * @param accessToken The JWT access token
     * @param refreshToken The refresh token (optional, may be null)
     * @param expiresInSeconds Number of seconds until access token expires
     */
    public synchronized void storeTokens(String accessToken, String refreshToken, long expiresInSeconds) {
        if (accessToken == null || accessToken.isEmpty()) {
            throw new IllegalArgumentException("accessToken is required");
        }
        if (expiresInSeconds <= 0) {
            throw new IllegalArgumentException("expiresInSeconds must be positive, got: " + expiresInSeconds);
        }

        this.accessToken = accessToken;
        this.refreshToken = refreshToken;
        this.expiresAt = Instant.now().plusSeconds(expiresInSeconds);
    }

    /**
     * Retrieves the access token if it's still valid.
     * Includes a safety buffer before actual expiration to prevent race conditions.
     *
     * @return Access token, or null if expired/missing
     */
    public synchronized String getAccessToken() {
        if (accessToken == null) {
            return null;
        }

        // Check if token is expired (with safety buffer)
        Instant bufferExpiry = expiresAt.minusSeconds(EXPIRATION_BUFFER_SECONDS);
        if (Instant.now().isAfter(bufferExpiry)) {
            return null;
        }

        return accessToken;
    }

    /**
     * Retrieves the refresh token.
     *
     * @return Refresh token, or null if not available
     */
    public synchronized String getRefreshToken() {
        return refreshToken;
    }

    /**
     * Checks if a valid access token is available.
     *
     * @return true if access token exists and is not expired
     */
    public synchronized boolean hasValidAccessToken() {
        return getAccessToken() != null;
    }

    /**
     * Clears all stored tokens.
     * Useful when logging out or when authentication fails.
     */
    public synchronized void clearTokens() {
        this.accessToken = null;
        this.refreshToken = null;
        this.expiresAt = null;
    }

    /**
     * Gets the expiration time of the current access token.
     *
     * @return Expiration instant, or null if no token stored
     */
    public synchronized Instant getExpiresAt() {
        return expiresAt;
    }
}
