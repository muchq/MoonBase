package com.muchq.mcpclient.oauth;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.mcpclient.McpClientConfig;
import com.nimbusds.oauth2.sdk.*;
import com.nimbusds.oauth2.sdk.auth.ClientAuthentication;
import com.nimbusds.oauth2.sdk.auth.ClientAuthenticationMethod;
import com.nimbusds.oauth2.sdk.auth.ClientSecretBasic;
import com.nimbusds.oauth2.sdk.auth.ClientSecretPost;
import com.nimbusds.oauth2.sdk.auth.Secret;
import com.nimbusds.oauth2.sdk.http.HTTPRequest;
import com.nimbusds.oauth2.sdk.http.HTTPResponse;
import com.nimbusds.oauth2.sdk.id.ClientID;
import com.nimbusds.oauth2.sdk.id.State;
import com.nimbusds.oauth2.sdk.pkce.CodeChallenge;
import com.nimbusds.oauth2.sdk.pkce.CodeChallengeMethod;
import com.nimbusds.oauth2.sdk.pkce.CodeVerifier;
import com.nimbusds.oauth2.sdk.token.AccessToken;
import com.nimbusds.oauth2.sdk.token.RefreshToken;
import com.nimbusds.openid.connect.sdk.OIDCTokenResponse;
import com.nimbusds.openid.connect.sdk.OIDCTokenResponseParser;
import com.nimbusds.openid.connect.sdk.rp.OIDCClientInformation;
import com.nimbusds.openid.connect.sdk.rp.OIDCClientInformationResponse;
import com.nimbusds.openid.connect.sdk.rp.OIDCClientMetadata;
import com.nimbusds.openid.connect.sdk.rp.OIDCClientRegistrationRequest;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Handles the complete OAuth 2.1 + PKCE + DCR flow for MCP authentication.
 *
 * Flow:
 * 1. Fetch Protected Resource Metadata (RFC 9728) from MCP server
 * 2. Fetch Authorization Server Metadata (RFC 8414) from Keycloak
 * 3. Dynamically Register Client (RFC 7591)
 * 4. Generate PKCE parameters
 * 5. Build authorization URL with resource parameter (RFC 8707)
 * 6. Open browser for user authentication
 * 7. Wait for authorization code via callback server
 * 8. Exchange code for tokens (with code_verifier + resource parameter)
 * 9. Store tokens in TokenManager
 */
public class OAuthFlowHandler {

    private static final Logger LOG = LoggerFactory.getLogger(OAuthFlowHandler.class);

    private final McpClientConfig config;
    private final TokenManager tokenManager;
    private final HttpClient httpClient;
    private final ObjectMapper objectMapper;

    public OAuthFlowHandler(McpClientConfig config, TokenManager tokenManager) {
        this.config = config;
        this.tokenManager = tokenManager;
        this.httpClient = HttpClient.newHttpClient();
        this.objectMapper = new ObjectMapper();
    }

    /**
     * Executes the complete OAuth 2.1 flow and stores tokens.
     *
     * @throws Exception if any step of the OAuth flow fails
     */
    public void executeFlow() throws Exception {
        LOG.info("Starting OAuth 2.1 + PKCE + DCR flow for MCP server: {}", config.getServerUrl());

        // Step 1: Fetch Protected Resource Metadata (RFC 9728)
        String resourceUri = extractResourceUri(config.getServerUrl());
        ProtectedResourceMetadata resourceMetadata = fetchProtectedResourceMetadata(resourceUri);
        LOG.info("Protected Resource: {}", resourceMetadata.resource());

        if (resourceMetadata.authorization_servers() == null || resourceMetadata.authorization_servers().isEmpty()) {
            throw new IOException("Protected Resource Metadata missing authorization_servers");
        }
        String authzServerUrl = resourceMetadata.authorization_servers().get(0);
        LOG.info("Authorization Server: {}", authzServerUrl);

        // Step 2: Fetch Authorization Server Metadata (RFC 8414)
        AuthorizationServerMetadata authzMetadata = fetchAuthorizationServerMetadata(authzServerUrl);
        LOG.info("Authorization Endpoint: {}", authzMetadata.authorization_endpoint());
        LOG.info("Token Endpoint: {}", authzMetadata.token_endpoint());
        LOG.info("Registration Endpoint: {}", authzMetadata.registration_endpoint());

        // Step 3: Dynamic Client Registration (RFC 7591)
        String redirectUri = "http://localhost:" + config.getCallbackPort() + "/callback";
        ClientRegistration clientRegistration = resolveClientRegistration(
            authzMetadata,
            redirectUri
        );
        LOG.info("Client ID: {}", clientRegistration.clientId().getValue());

        // Step 4: Generate PKCE parameters
        PkceGenerator.PkceParams pkce = PkceGenerator.generate();
        CodeVerifier codeVerifier = new CodeVerifier(pkce.codeVerifier());
        CodeChallenge codeChallenge = CodeChallenge.compute(CodeChallengeMethod.S256, codeVerifier);

        // Step 5: Build authorization request with resource parameter (RFC 8707)
        State state = new State();
        AuthorizationRequest authzRequest = new AuthorizationRequest.Builder(
                new ResponseType(ResponseType.Value.CODE),
                clientRegistration.clientId()
            )
            .endpointURI(URI.create(authzMetadata.authorization_endpoint()))
            .redirectionURI(URI.create(redirectUri))
            .codeChallenge(codeChallenge, CodeChallengeMethod.S256)
            .customParameter("resource", resourceUri)  // RFC 8707 - CRITICAL!
            .state(state)
            .scope(new Scope("openid"))
            .build();

        String authzUrl = authzRequest.toURI().toString();
        LOG.info("Authorization URL: {}", authzUrl);

        // Step 6: Start local callback server
        CallbackServer callbackServer = new CallbackServer(config.getCallbackPort(), state.getValue());
        callbackServer.start();

        try {
            // Step 7: Open browser for user authentication
            LOG.info("Opening browser for user authentication...");
            BrowserLauncher.open(authzUrl);

            // Step 8: Wait for authorization code
            LOG.info("Waiting for authorization code (timeout: 5 minutes)...");
            String authCode = callbackServer.waitForAuthorizationCode(5, TimeUnit.MINUTES);
            LOG.info("Authorization code received");

            // Step 9: Exchange code for tokens with resource parameter
            OAuthTokenResponse tokens = exchangeCodeForTokens(
                authzMetadata.token_endpoint(),
                clientRegistration,
                authCode,
                redirectUri,
                codeVerifier,
                resourceUri  // RFC 8707 - CRITICAL!
            );

            // Step 10: Store tokens
            long expiresIn = tokens.access_token_expires_in() != null ?
                tokens.access_token_expires_in() : 300L; // Default 5 minutes

            tokenManager.storeTokens(
                tokens.access_token(),
                tokens.refresh_token(),
                expiresIn
            );

            LOG.info("OAuth flow completed successfully!");
            LOG.info("Access token expires in {} seconds", expiresIn);

        } finally {
            // Always stop the callback server
            callbackServer.stop();
        }
    }

    /**
     * Fetches Protected Resource Metadata (RFC 9728) from MCP server.
     */
    private ProtectedResourceMetadata fetchProtectedResourceMetadata(String resourceUri) throws Exception {
        String metadataUrl = resourceUri + "/.well-known/oauth-protected-resource";
        LOG.debug("Fetching Protected Resource Metadata: {}", metadataUrl);

        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(metadataUrl))
            .GET()
            .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());

        if (response.statusCode() != 200) {
            throw new IOException("Failed to fetch Protected Resource Metadata: HTTP " + response.statusCode());
        }

        return objectMapper.readValue(response.body(), ProtectedResourceMetadata.class);
    }

    /**
     * Fetches Authorization Server Metadata (RFC 8414) from Keycloak.
     */
    private AuthorizationServerMetadata fetchAuthorizationServerMetadata(String authzServerUrl) throws Exception {
        String metadataUrl = authzServerUrl + "/.well-known/openid-configuration";
        LOG.debug("Fetching Authorization Server Metadata: {}", metadataUrl);

        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(metadataUrl))
            .GET()
            .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());

        if (response.statusCode() != 200) {
            throw new IOException("Failed to fetch Authorization Server Metadata: HTTP " + response.statusCode());
        }

        return objectMapper.readValue(response.body(), AuthorizationServerMetadata.class);
    }

    /**
     * Registers a new OAuth client dynamically (RFC 7591).
     */
    private ClientRegistration resolveClientRegistration(
        AuthorizationServerMetadata authzMetadata,
        String redirectUri
    ) throws Exception {
        String configuredClientId = config.getClientId();
        if (configuredClientId != null && !configuredClientId.isEmpty()) {
            LOG.info("Using preconfigured client ID; skipping dynamic registration.");
            Secret secret = null;
            ClientAuthenticationMethod authMethod = ClientAuthenticationMethod.NONE;
            if (config.getClientSecret() != null && !config.getClientSecret().isEmpty()) {
                secret = new Secret(config.getClientSecret());
                authMethod = ClientAuthenticationMethod.CLIENT_SECRET_BASIC;
            }
            return new ClientRegistration(new ClientID(configuredClientId), secret, authMethod);
        }

        if (authzMetadata.registration_endpoint() == null) {
            throw new IOException("Authorization server does not advertise a registration endpoint.");
        }

        return registerClient(
            authzMetadata.registration_endpoint(),
            config.getClientName(),
            redirectUri
        );
    }

    private ClientRegistration registerClient(String registrationEndpoint, String clientName, String redirectUri)
        throws Exception {
        LOG.info("Registering client dynamically: {}", clientName);

        OIDCClientMetadata metadata = new OIDCClientMetadata();
        metadata.setName(clientName);
        metadata.setRedirectionURIs(Set.of(URI.create(redirectUri)));
        metadata.setGrantTypes(Set.of(GrantType.AUTHORIZATION_CODE, GrantType.REFRESH_TOKEN));
        metadata.setResponseTypes(Set.of(new ResponseType(ResponseType.Value.CODE)));
        metadata.setTokenEndpointAuthMethod(ClientAuthenticationMethod.NONE);

        OIDCClientRegistrationRequest regRequest = new OIDCClientRegistrationRequest(
            URI.create(registrationEndpoint),
            metadata,
            null
        );

        HTTPRequest httpRequest = regRequest.toHTTPRequest();
        HTTPResponse httpResponse = httpRequest.send();

        OIDCClientInformationResponse regResponse = OIDCClientInformationResponse.parse(httpResponse);

        if (!regResponse.indicatesSuccess()) {
            throw new IOException("Client registration failed: " + regResponse.toErrorResponse().getErrorObject());
        }

        OIDCClientInformation clientInfo =
            (OIDCClientInformation) regResponse.toSuccessResponse().getClientInformation();
        ClientAuthenticationMethod authMethod = clientInfo.getOIDCMetadata().getTokenEndpointAuthMethod();
        return new ClientRegistration(clientInfo.getID(), clientInfo.getSecret(), authMethod);
    }

    /**
     * Exchanges authorization code for access token (with PKCE + resource parameter).
     */
    private OAuthTokenResponse exchangeCodeForTokens(
        String tokenEndpoint,
        ClientRegistration clientRegistration,
        String authCode,
        String redirectUri,
        CodeVerifier codeVerifier,
        String resourceUri
    ) throws Exception {
        LOG.info("Exchanging authorization code for tokens...");

        // Build token request with PKCE
        AuthorizationGrant grant = new AuthorizationCodeGrant(
            new AuthorizationCode(authCode),
            URI.create(redirectUri),
            codeVerifier
        );

        ClientAuthentication clientAuthentication = buildClientAuthentication(clientRegistration);

        com.nimbusds.oauth2.sdk.TokenRequest tokenRequest;
        List<URI> resources = List.of(URI.create(resourceUri));  // RFC 8707 resource parameter - CRITICAL!

        if (clientAuthentication == null) {
            tokenRequest = new com.nimbusds.oauth2.sdk.TokenRequest(
                URI.create(tokenEndpoint),
                clientRegistration.clientId(),
                grant,
                null,
                resources,
                null,
                null
            );
        } else {
            tokenRequest = new com.nimbusds.oauth2.sdk.TokenRequest(
                URI.create(tokenEndpoint),
                clientAuthentication,
                grant,
                null,
                resources,
                null
            );
        }

        HTTPRequest httpRequest = tokenRequest.toHTTPRequest();
        HTTPResponse httpResponse = httpRequest.send();

        // Parse response
        com.nimbusds.oauth2.sdk.TokenResponse tokenResponse = OIDCTokenResponseParser.parse(httpResponse);

        if (!tokenResponse.indicatesSuccess()) {
            throw new IOException("Token exchange failed: " + tokenResponse.toErrorResponse().getErrorObject());
        }

        OIDCTokenResponse successResponse = (OIDCTokenResponse) tokenResponse.toSuccessResponse();
        AccessToken accessToken = successResponse.getOIDCTokens().getAccessToken();
        RefreshToken refreshToken = successResponse.getOIDCTokens().getRefreshToken();

        return new OAuthTokenResponse(
            accessToken.getValue(),
            refreshToken != null ? refreshToken.getValue() : null,
            accessToken.getLifetime()
        );
    }

    /**
     * Extracts resource URI from server URL.
     * Removes path component to get base URL.
     */
    private String extractResourceUri(String serverUrl) {
        try {
            URI uri = URI.create(serverUrl);
            return uri.getScheme() + "://" + uri.getAuthority();
        } catch (Exception e) {
            // Fallback: return as-is
            return serverUrl;
        }
    }

    private ClientAuthentication buildClientAuthentication(ClientRegistration clientRegistration) throws Exception {
        if (clientRegistration == null) {
            return null;
        }

        ClientAuthenticationMethod authMethod = clientRegistration.authMethod();
        Secret clientSecret = clientRegistration.clientSecret();

        if (authMethod == null || ClientAuthenticationMethod.NONE.equals(authMethod)) {
            return null;
        }

        if (clientSecret == null) {
            throw new IOException("Token endpoint requires client authentication, but no client secret was provided.");
        }

        if (ClientAuthenticationMethod.CLIENT_SECRET_BASIC.equals(authMethod)) {
            return new ClientSecretBasic(clientRegistration.clientId(), clientSecret);
        }

        if (ClientAuthenticationMethod.CLIENT_SECRET_POST.equals(authMethod)) {
            return new ClientSecretPost(clientRegistration.clientId(), clientSecret);
        }

        throw new IOException("Unsupported token endpoint auth method: " + authMethod.getValue());
    }

    // DTOs for metadata and tokens

    public record ProtectedResourceMetadata(
        String resource,
        List<String> authorization_servers,
        List<String> bearer_methods_supported
    ) {}

    @com.fasterxml.jackson.annotation.JsonIgnoreProperties(ignoreUnknown = true)
    public record AuthorizationServerMetadata(
        String issuer,
        String authorization_endpoint,
        String token_endpoint,
        String registration_endpoint,
        String jwks_uri
    ) {}

    public record OAuthTokenResponse(
        String access_token,
        String refresh_token,
        Long access_token_expires_in
    ) {}

    private record ClientRegistration(
        ClientID clientId,
        Secret clientSecret,
        ClientAuthenticationMethod authMethod
    ) {}
}
