# YoChat

### client
```
openssl s_client -connect <domain>
```

### nginx
```
stream {
  server {
    listen     443;
    proxy_pass localhost:8992;
  }
}
```
