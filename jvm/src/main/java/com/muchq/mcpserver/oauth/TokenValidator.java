package com.muchq.mcpserver.oauth;

import com.nimbusds.jose.JOSEException;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.JWSVerifier;
import com.nimbusds.jose.crypto.RSASSAVerifier;
import com.nimbusds.jose.jwk.JWK;
import com.nimbusds.jose.jwk.JWKSet;
import com.nimbusds.jose.jwk.RSAKey;
import com.nimbusds.jwt.JWTClaimsSet;
import com.nimbusds.jwt.SignedJWT;
import io.micronaut.context.annotation.Requires;
import jakarta.inject.Singleton;
import java.io.IOException;
import java.net.URL;
import java.text.ParseException;
import java.util.Date;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Validates JWT access tokens from Keycloak.
 *
 * This validator performs three critical checks:
 * 1. Signature verification against Keycloak's JWKS
 * 2. Expiration check
 * 3. Audience validation (RFC 8707) - CRITICAL for security
 *
 * The audience check ensures tokens issued for other services
 * cannot be used to access this MCP server.
 */
@Singleton
@Requires(property = "mcp.oauth.enabled", value = "true")
public class TokenValidator {

    private static final Logger LOG = LoggerFactory.getLogger(TokenValidator.class);

    private final OAuthConfig config;
    private final JWKSet jwkSet;

    public TokenValidator(OAuthConfig config) throws IOException, ParseException {
        this.config = config;

        // Fetch JWKS from Keycloak
        LOG.info("Fetching JWKS from: {}", config.getJwksUri());
        this.jwkSet = JWKSet.load(new URL(config.getJwksUri()));
        LOG.info("Loaded {} keys from JWKS", jwkSet.getKeys().size());
    }

    /**
     * Validates a JWT access token.
     *
     * @param bearerToken The JWT token (without "Bearer " prefix)
     * @return ValidationResult indicating success or failure with error message
     */
    public ValidationResult validate(String bearerToken) {
        try {
            // Parse JWT
            SignedJWT jwt = SignedJWT.parse(bearerToken);
            JWTClaimsSet claims = jwt.getJWTClaimsSet();

            LOG.debug("Validating token: kid={}, sub={}, aud={}",
                jwt.getHeader().getKeyID(),
                claims.getSubject(),
                claims.getAudience());

            // 1. Verify signature
            if (!verifySignature(jwt)) {
                LOG.warn("Token signature verification failed");
                return ValidationResult.invalid("Invalid signature");
            }

            // 2. Check expiration
            Date expiration = claims.getExpirationTime();
            if (expiration == null) {
                LOG.warn("Token missing expiration claim");
                return ValidationResult.invalid("Missing expiration");
            }

            if (expiration.before(new Date())) {
                LOG.warn("Token expired at: {}", expiration);
                return ValidationResult.invalid("Token expired");
            }

            // 3. CRITICAL: Validate audience (RFC 8707)
            // This prevents tokens issued for other services from being accepted
            List<String> audiences = claims.getAudience();
            if (audiences == null || audiences.isEmpty()) {
                LOG.warn("Token missing audience claim");
                return ValidationResult.invalid("Missing audience");
            }

            String expectedAudience = config.getResourceUri();
            if (!audiences.contains(expectedAudience)) {
                LOG.warn("Token audience mismatch. Expected: {}, Got: {}",
                    expectedAudience, audiences);
                return ValidationResult.invalid("Invalid audience");
            }

            LOG.info("Token validated successfully for subject: {}", claims.getSubject());
            return ValidationResult.valid(claims.getSubject());

        } catch (ParseException e) {
            LOG.error("Failed to parse JWT token", e);
            return ValidationResult.invalid("Malformed token");
        } catch (Exception e) {
            LOG.error("Token validation failed", e);
            return ValidationResult.invalid("Validation error: " + e.getMessage());
        }
    }

    /**
     * Verifies the JWT signature against Keycloak's public keys.
     *
     * @param jwt The signed JWT
     * @return true if signature is valid, false otherwise
     */
    private boolean verifySignature(SignedJWT jwt) {
        try {
            String keyId = jwt.getHeader().getKeyID();
            JWSAlgorithm algorithm = jwt.getHeader().getAlgorithm();

            if (keyId == null) {
                LOG.warn("Token missing 'kid' header");
                return false;
            }

            // Find the matching key in JWKS
            JWK jwk = jwkSet.getKeyByKeyId(keyId);
            if (jwk == null) {
                LOG.warn("No key found for kid: {}", keyId);
                return false;
            }

            // Only support RSA for now (Keycloak default)
            if (!(jwk instanceof RSAKey)) {
                LOG.warn("Unsupported key type: {}", jwk.getKeyType());
                return false;
            }

            RSAKey rsaKey = (RSAKey) jwk;
            JWSVerifier verifier = new RSASSAVerifier(rsaKey.toRSAPublicKey());

            return jwt.verify(verifier);

        } catch (JOSEException e) {
            LOG.error("Signature verification failed", e);
            return false;
        }
    }

    /**
     * Result of token validation.
     *
     * @param valid Whether the token is valid
     * @param errorMessage Error message if invalid, null otherwise
     * @param subject The subject (username) from the token if valid, null otherwise
     */
    public static class ValidationResult {
        private final boolean valid;
        private final String errorMessage;
        private final String subject;

        private ValidationResult(boolean valid, String errorMessage, String subject) {
            this.valid = valid;
            this.errorMessage = errorMessage;
            this.subject = subject;
        }

        public static ValidationResult valid(String subject) {
            return new ValidationResult(true, null, subject);
        }

        public static ValidationResult invalid(String errorMessage) {
            return new ValidationResult(false, errorMessage, null);
        }

        public boolean isValid() {
            return valid;
        }

        public String getErrorMessage() {
            return errorMessage;
        }

        public String getSubject() {
            return subject;
        }
    }
}
