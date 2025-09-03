# r3dr Migration to api.muchq.com

This document outlines the steps to consolidate r3dr from its dedicated machine to api.muchq.com.

## Current Setup Analysis

### r3dr Current Deployment
- **Domain**: r3dr.net (with www redirect)
- **Service Port**: 8080 (internally)
- **Docker Image**: `ghcr.io/muchq/r3dr:latest`
- **Database Config**: 
  - Via env var `DB_CONNECTION_STRING` or 
  - Config file at `/etc/r3dr/db_config`
- **Web Assets**: Served from `/var/www/r3dr`
- **API Endpoints**:
  - `POST /shorten` - URL shortening
  - `GET /r/*` - Redirects

### api.muchq.com Current Setup
- **Existing Services**: 
  - games_ws_backend (port 8080)
  - portrait (port 8081)
  - Caddy reverse proxy
- **Network**: `muchq_network` bridge network
- **Observability**: OpenTelemetry collection enabled

## Migration Plan

### 1. Pre-Migration Setup

#### Database Configuration
1. **Determine r3dr database requirements**
   - Check current database used by r3dr
   - Ensure database is accessible from api.muchq.com or migrate data

2. **Create r3dr config directory on api.muchq.com**
   ```bash
   ssh ubuntu@api.muchq.com "sudo mkdir -p /etc/r3dr"
   ```

3. **Copy database configuration**
   ```bash
   # Copy from current r3dr machine to api.muchq.com
   scp ubuntu@<current-r3dr-machine>:/etc/r3dr/db_config ubuntu@api.muchq.com:~/
   ssh ubuntu@api.muchq.com "sudo mv ~/db_config /etc/r3dr/"
   ```

#### Web Assets Migration
1. **Create web assets directory**
   ```bash
   ssh ubuntu@api.muchq.com "sudo mkdir -p /var/www/r3dr"
   ```

2. **Copy web assets from repository**
   ```bash
   # Copy from local repo or build assets
   scp -r web/r3dr/* ubuntu@api.muchq.com:~/r3dr_assets/
   ssh ubuntu@api.muchq.com "sudo cp -r ~/r3dr_assets/* /var/www/r3dr/ && sudo chown -R www-data:www-data /var/www/r3dr"
   ```

### 2. Update Docker Compose Configuration

#### Modify `/deploy/api.muchq.com/compose.yaml`
Add r3dr service to the existing compose file:

```yaml
services:
  # ... existing services ...
  
  r3dr:
    image: ghcr.io/muchq/r3dr:latest
    restart: always
    ports:
      - "127.0.0.1:8082:8080"  # Use port 8082 to avoid conflicts
    volumes:
      - /etc/r3dr:/etc/r3dr
    networks:
      - app_network
    depends_on:
      - caddy

  # Update caddy volumes to include r3dr web assets
  caddy:
    # ... existing config ...
    volumes:
      - ./Caddyfile:/etc/caddy/Caddyfile:ro
      - caddy_data:/data
      - caddy_config:/config
      - /var/log/caddy:/var/log/caddy
      - /var/www/r3dr:/var/www/r3dr:ro  # Add this line
```

### 3. Update Caddy Configuration

#### Modify `/deploy/api.muchq.com/Caddyfile`
Add r3dr routing while maintaining existing functionality:

```caddyfile
api.muchq.com {
    # Existing portrait and options handlers
    @post_portrait {
        method POST
        path /v1/trace
    }

    @options {
        method OPTIONS
    }

    # r3dr handlers
    @r3dr_shorten {
        method POST
        path /shorten
    }
    
    @r3dr_redirect {
        method GET
        path /r/*
    }

    @r3dr_static {
        path /r3dr/*
    }

    # CORS headers (existing)
    header {
        Access-Control-Allow-Origin "https://muchq.com"
        Access-Control-Allow-Methods "GET, POST, OPTIONS"
        Access-Control-Allow-Headers "Content-Type, Authorization"
        Access-Control-Max-Age "86400"
    }

    # Handle OPTIONS (existing)
    handle @options {
        header {
            Access-Control-Allow-Origin "https://muchq.com"
            Access-Control-Allow-Methods "GET, POST, OPTIONS"
            Access-Control-Allow-Headers "Content-Type, Authorization"
            Access-Control-Max-Age "86400"
        }
        respond 204
    }

    # r3dr API endpoints
    handle @r3dr_shorten {
        reverse_proxy r3dr:8080
    }
    
    handle @r3dr_redirect {
        reverse_proxy r3dr:8080
    }

    # r3dr static files (served at /r3dr/ path)
    handle @r3dr_static {
        root * /var/www/r3dr
        uri strip_prefix /r3dr
        file_server
    }

    # Portrait API (existing)
    reverse_proxy @post_portrait portrait:8081 {
        header_down -Access-Control-Allow-Origin
        header_down -Access-Control-Allow-Methods
        header_down -Access-Control-Allow-Headers
        header_down -Access-Control-Max-Age
    }
    
    # Games backend (existing - fallback)
    reverse_proxy games_ws_backend:8080
    
    log {
        output file /var/log/caddy/access.log {
            roll_size 1gb
            roll_keep 5
            roll_keep_for 90d
        }
    }
}
```

### 4. Domain Migration Strategy

#### Option A: Gradual Migration (Recommended)
1. **Deploy r3dr on api.muchq.com first**
2. **Test functionality at** `api.muchq.com/r3dr/` (web UI) and API endpoints
3. **Update DNS for r3dr.net** to point to api.muchq.com
4. **Update Caddyfile** to handle r3dr.net domain:

```caddyfile
# Add this block for seamless domain support
r3dr.net, www.r3dr.net {
    redir @www https://r3dr.net{uri} 301
    
    @www {
        host www.r3dr.net
    }
    
    @shorten {
        method POST
        path /shorten
    }
    
    @redirect {
        method GET
        path /r/*
    }
    
    root * /var/www/r3dr
    
    reverse_proxy @shorten r3dr:8080
    reverse_proxy @redirect r3dr:8080
    
    file_server
    
    log {
        output file /var/log/caddy/r3dr_access.log {
            roll_size 1gb
            roll_keep 5
            roll_keep_for 90d
        }
    }
}
```

#### Option B: Direct Path Integration
Keep r3dr accessible only via `api.muchq.com/r3dr/*` and update frontend apps accordingly.

### 5. Deployment Steps

1. **Update compose and Caddy files** locally
2. **Deploy to api.muchq.com**:
   ```bash
   cd /path/to/MoonBase
   ./deploy/api.muchq.com/deploy.sh
   ```
3. **Verify r3dr is running**:
   ```bash
   ssh ubuntu@api.muchq.com "sudo docker compose logs r3dr"
   ```
4. **Test API endpoints**:
   ```bash
   # Test shortening
   curl -X POST https://api.muchq.com/shorten \
     -H "Content-Type: application/json" \
     -d '{"longUrl": "https://example.com"}'
   
   # Test web interface (if using path integration)
   curl https://api.muchq.com/r3dr/
   ```

### 6. DNS Migration (if using Option A)
1. **Update DNS A records** for r3dr.net and www.r3dr.net to point to api.muchq.com IP
2. **Wait for DNS propagation** (up to 48 hours)
3. **Monitor logs** for traffic on new server
4. **Shutdown old r3dr machine** once traffic has migrated

### 7. Post-Migration Cleanup

1. **Remove old r3dr infrastructure**
2. **Update any hardcoded references** to old r3dr URLs in other services
3. **Monitor performance** and resource usage on api.muchq.com
4. **Update monitoring/alerts** to include r3dr service

## Configuration Files to Modify

1. `deploy/api.muchq.com/compose.yaml` - Add r3dr service
2. `deploy/api.muchq.com/Caddyfile` - Add routing rules
3. DNS records for r3dr.net (if maintaining domain)

## Rollback Plan

If issues arise:
1. **Revert DNS** changes to point back to original r3dr machine
2. **Remove r3dr service** from api.muchq.com compose file
3. **Redeploy api.muchq.com** without r3dr
4. **Restart original r3dr machine**

## Testing Checklist

- [ ] r3dr container starts successfully
- [ ] Database connectivity works
- [ ] URL shortening API responds correctly
- [ ] Redirects work properly
- [ ] Web interface loads and functions
- [ ] Logs are being written correctly
- [ ] No conflicts with existing services
- [ ] SSL certificates work for all domains
- [ ] Performance is acceptable under load

## Considerations

- **Resource Usage**: Monitor CPU/memory impact on api.muchq.com
- **Database Performance**: Ensure r3dr database calls don't impact other services
- **Backup Strategy**: Include r3dr data in backup procedures
- **Monitoring**: Add r3dr metrics to existing observability setup