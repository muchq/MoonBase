# r3dr - Another URL Shortener

[r3dr.net](https://r3dr.net)

### Shorten
`POST /shorten`
```json
{
  "longUrl": "https://www.google.com",
  "expiresAt": 1728445884000
}
```

`expiresAt` is optional.

### Redirect
`GET /r/{slug} -> 302` so we can collect stats

### TODO
- vanity urls
- stats
- cache
- rate limits
- clean-up worker (archive/delete expired stuff)
- logs
- request tracing

### Reusing expired slugs
Currently, there's no way to reclaim the nice short slugs associated with low ids even after the redirects expire.
