package com.muchq.mcpclient.oauth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.Test;

public class TokenManagerTest {

    @Test
    public void testStoreAndRetrieveTokens() {
        TokenManager manager = new TokenManager();

        manager.storeTokens("access123", "refresh456", 300);

        assertThat(manager.getAccessToken()).isEqualTo("access123");
        assertThat(manager.getRefreshToken()).isEqualTo("refresh456");
        assertThat(manager.hasValidAccessToken()).isTrue();
    }

    @Test
    public void testStoreTokensWithNullRefreshToken() {
        TokenManager manager = new TokenManager();

        manager.storeTokens("access123", null, 300);

        assertThat(manager.getAccessToken()).isEqualTo("access123");
        assertThat(manager.getRefreshToken()).isNull();
        assertThat(manager.hasValidAccessToken()).isTrue();
    }

    @Test
    public void testGetAccessTokenReturnsNullWhenNotSet() {
        TokenManager manager = new TokenManager();

        assertThat(manager.getAccessToken()).isNull();
        assertThat(manager.hasValidAccessToken()).isFalse();
    }

    @Test
    public void testGetAccessTokenReturnsNullWhenExpired() {
        TokenManager manager = new TokenManager();

        // Store token that expires in 1 second (but buffer is 30 seconds, so it's already "expired")
        manager.storeTokens("access123", "refresh456", 1);

        // Token should be considered expired due to 30-second buffer
        assertThat(manager.getAccessToken()).isNull();
        assertThat(manager.hasValidAccessToken()).isFalse();
    }

    @Test
    public void testGetAccessTokenWithSufficientTime() {
        TokenManager manager = new TokenManager();

        // Store token that expires in 60 seconds (more than 30-second buffer)
        manager.storeTokens("access123", "refresh456", 60);

        assertThat(manager.getAccessToken()).isEqualTo("access123");
        assertThat(manager.hasValidAccessToken()).isTrue();
    }

    @Test
    public void testClearTokens() {
        TokenManager manager = new TokenManager();
        manager.storeTokens("access123", "refresh456", 300);

        manager.clearTokens();

        assertThat(manager.getAccessToken()).isNull();
        assertThat(manager.getRefreshToken()).isNull();
        assertThat(manager.hasValidAccessToken()).isFalse();
        assertThat(manager.getExpiresAt()).isNull();
    }

    @Test
    public void testStoreTokensRejectsNullAccessToken() {
        TokenManager manager = new TokenManager();

        assertThatThrownBy(() -> manager.storeTokens(null, "refresh", 300))
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("accessToken is required");
    }

    @Test
    public void testStoreTokensRejectsEmptyAccessToken() {
        TokenManager manager = new TokenManager();

        assertThatThrownBy(() -> manager.storeTokens("", "refresh", 300))
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("accessToken is required");
    }

    @Test
    public void testStoreTokensRejectsZeroExpiration() {
        TokenManager manager = new TokenManager();

        assertThatThrownBy(() -> manager.storeTokens("access", "refresh", 0))
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("expiresInSeconds must be positive");
    }

    @Test
    public void testStoreTokensRejectsNegativeExpiration() {
        TokenManager manager = new TokenManager();

        assertThatThrownBy(() -> manager.storeTokens("access", "refresh", -1))
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("expiresInSeconds must be positive");
    }

    @Test
    public void testGetExpiresAtReturnsNullInitially() {
        TokenManager manager = new TokenManager();

        assertThat(manager.getExpiresAt()).isNull();
    }

    @Test
    public void testGetExpiresAtReturnsValueAfterStore() {
        TokenManager manager = new TokenManager();

        manager.storeTokens("access123", "refresh456", 300);

        assertThat(manager.getExpiresAt()).isNotNull();
    }

    @Test
    public void testRefreshTokenPersistsAfterAccessTokenExpires() {
        TokenManager manager = new TokenManager();

        // Store token with very short expiration
        manager.storeTokens("access123", "refresh456", 1);

        // Access token should be null (expired due to buffer), but refresh token should persist
        assertThat(manager.getAccessToken()).isNull();
        assertThat(manager.getRefreshToken()).isEqualTo("refresh456");
    }
}
