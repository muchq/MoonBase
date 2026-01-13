package com.muchq.mcpclient.oauth;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Base64;

/**
 * Generates PKCE (Proof Key for Code Exchange) parameters for OAuth 2.1.
 *
 * PKCE prevents authorization code interception attacks by binding
 * the authorization request to the token request through a cryptographic challenge.
 *
 * RFC 7636: https://datatracker.ietf.org/doc/html/rfc7636
 */
public class PkceGenerator {

    private static final SecureRandom SECURE_RANDOM = new SecureRandom();
    private static final int CODE_VERIFIER_LENGTH = 32; // 32 bytes = 43 base64url chars

    /**
     * Generates PKCE parameters with S256 code challenge method.
     *
     * Flow:
     * 1. Generate cryptographically random code_verifier (43-128 characters)
     * 2. Compute code_challenge = BASE64URL(SHA256(ASCII(code_verifier)))
     * 3. Use code_challenge in authorization request
     * 4. Use code_verifier in token request
     *
     * @return PkceParams containing code_verifier, code_challenge, and method
     */
    public static PkceParams generate() {
        try {
            // 1. Generate cryptographically random code_verifier
            byte[] randomBytes = new byte[CODE_VERIFIER_LENGTH];
            SECURE_RANDOM.nextBytes(randomBytes);

            String codeVerifier = Base64.getUrlEncoder()
                .withoutPadding()
                .encodeToString(randomBytes);

            // 2. Compute S256 code_challenge
            MessageDigest sha256 = MessageDigest.getInstance("SHA-256");
            byte[] hash = sha256.digest(codeVerifier.getBytes(StandardCharsets.US_ASCII));

            String codeChallenge = Base64.getUrlEncoder()
                .withoutPadding()
                .encodeToString(hash);

            return new PkceParams(codeVerifier, codeChallenge, "S256");

        } catch (NoSuchAlgorithmException e) {
            // SHA-256 is always available in modern JVMs
            throw new RuntimeException("SHA-256 algorithm not available", e);
        }
    }

    /**
     * PKCE parameters for OAuth authorization code flow.
     *
     * @param codeVerifier Random string (43-128 characters) sent in token request
     * @param codeChallenge Base64url(SHA256(codeVerifier)) sent in authorization request
     * @param codeChallengeMethod Always "S256" for SHA-256
     */
    public record PkceParams(
        String codeVerifier,
        String codeChallenge,
        String codeChallengeMethod
    ) {
        public PkceParams {
            if (codeVerifier == null || codeVerifier.length() < 43 || codeVerifier.length() > 128) {
                throw new IllegalArgumentException(
                    "code_verifier must be 43-128 characters, got: " +
                    (codeVerifier == null ? "null" : codeVerifier.length())
                );
            }
            if (codeChallenge == null || codeChallenge.isEmpty()) {
                throw new IllegalArgumentException("code_challenge is required");
            }
            if (!"S256".equals(codeChallengeMethod)) {
                throw new IllegalArgumentException(
                    "Only S256 code challenge method is supported, got: " + codeChallengeMethod
                );
            }
        }
    }
}
