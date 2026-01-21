# Dynamic Client Registration in MCP

This document explains when and why to use OAuth 2.0 Dynamic Client Registration (DCR) with Model Context Protocol (MCP) servers.

## What is DCR?

Dynamic Client Registration ([RFC 7591](https://datatracker.ietf.org/doc/html/rfc7591)) allows OAuth clients to register themselves with an authorization server programmatically, rather than requiring manual pre-registration.

## When DCR Makes Sense

| Context | Why DCR Works |
|---------|---------------|
| **Developer tools/IDEs** | Claude Code, VS Code extensions, etc. can self-register without IT creating OAuth clients for each developer |
| **Desktop/CLI apps** | Each installation gets unique credentials; no shared client secrets baked into distributed binaries |
| **Multi-tenant platforms** | Client apps connecting to many different organizations' MCP servers can register with each tenant's auth server |
| **Self-service AI tooling** | Teams can deploy MCP-enabled tools without filing tickets to get OAuth clients provisioned |
| **Open ecosystems** | Public MCP servers that want to allow any compatible client to connect |

## When to Use Pre-registered Clients Instead

| Context | Why Skip DCR |
|---------|--------------|
| **High-security environments** | All clients must be vetted before access |
| **Production backends** | Server-to-server MCP calls where clients are known and fixed |
| **Audit requirements** | Need explicit control over which clients exist |
| **Rate limiting by client** | Want to assign quotas to specific known clients |

## Open Ecosystem Pattern

The most interesting DCR use case is building **public MCP servers that any compatible client can connect to**, similar to how public REST APIs work with OAuth.

### Example: Public MCP Server

```
https://api.example.com/mcp
├── tools/
│   ├── weather_forecast
│   ├── stock_quotes
│   └── flight_search
```

### The Flow

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Any MCP       │     │  Your Auth      │     │  Your MCP       │
│   Client        │     │  Server         │     │  Server         │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         │ 1. GET /.well-known/oauth-protected-resource │
         │──────────────────────────────────────────────>│
         │                       │                       │
         │ 2. Discover auth server                       │
         │<──────────────────────────────────────────────│
         │                       │                       │
         │ 3. POST /register (DCR)                       │
         │──────────────────────>│                       │
         │                       │                       │
         │ 4. client_id assigned │                       │
         │<──────────────────────│                       │
         │                       │                       │
         │ 5. User authenticates │                       │
         │──────────────────────>│                       │
         │                       │                       │
         │ 6. Access token       │                       │
         │<──────────────────────│                       │
         │                       │                       │
         │ 7. Call tools with token                      │
         │──────────────────────────────────────────────>│
```

### What DCR Enables

| Without DCR | With DCR |
|-------------|----------|
| "Email us to request API access" | Client self-registers instantly |
| Manual client_id provisioning | Automated client_id assignment |
| You maintain client registry | Auth server handles it |
| Friction for new integrations | Zero-friction onboarding |

### The Key Insight

DCR separates two concerns:

1. **Client identity** - "What software is making requests?" (handled by DCR)
2. **User identity** - "Who authorized this access?" (handled by OAuth login)

In an open ecosystem, you don't gate on *which client* is connecting. You gate on:
- Is the **user** authenticated?
- Does the **user** have permission to use these tools?
- Is the **user** within rate limits?

### Real-World Analogues

| Service | Pattern |
|---------|---------|
| Twitter API | Any app can register, users authorize access to their account |
| GitHub OAuth Apps | Any developer can create an app, users grant repo access |
| Google APIs | Register app in console, users consent to scopes |

MCP + DCR enables the same pattern for AI tool ecosystems.

## Implementation

This MCP client supports both patterns:

```java
// DCR (automatic registration)
McpClientConfig config = McpClientConfig.builder()
    .serverUrl("https://api.example.com/mcp")
    .clientName("My AI Tool")
    .build();

// Pre-registered (skip DCR)
McpClientConfig config = McpClientConfig.builder()
    .serverUrl("https://api.example.com/mcp")
    .clientId("known-client-id")
    .clientSecret("secret")
    .build();
```

## Standards References

- [RFC 7591](https://datatracker.ietf.org/doc/html/rfc7591) - OAuth 2.0 Dynamic Client Registration
- [RFC 7636](https://datatracker.ietf.org/doc/html/rfc7636) - PKCE (Proof Key for Code Exchange)
- [RFC 8414](https://datatracker.ietf.org/doc/html/rfc8414) - OAuth 2.0 Authorization Server Metadata
- [RFC 8707](https://datatracker.ietf.org/doc/html/rfc8707) - Resource Indicators for OAuth 2.0
- [RFC 9728](https://datatracker.ietf.org/doc/html/rfc9728) - OAuth 2.0 Protected Resource Metadata
