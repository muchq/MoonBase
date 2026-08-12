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

`expiresAt` is **required**, despite what this file said until now. Omitting it
sends Go's zero value, which decodes as epoch 0 — in the past — so the request
is rejected with `expiration time is in the past`. The web app never hit this
because it always sends now+7d. Making it genuinely optional with a server-side
default is a behavior change and belongs in its own commit; see
[#1359](https://github.com/muchq/MoonBase/issues/1359). `wire_test.go` pins the
current behavior either way.

Note also that `expires_at` is written and never read: `GetLongUrl` selects on
`short_url` alone, so **redirects do not currently expire**. Same issue.

## Model

[`model/r3dr.smithy`](model/r3dr.smithy) describes this API in Smithy, for the
in-progress rewrite onto smithy-cpp (#1359). It models the contract as served
today, including the two defects above, so the cutover's replay has no
unexplained diffs.

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
