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
 - shorter short urls
 - stats
 - logs
 - cache
 - rate limits
