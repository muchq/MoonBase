# Logging
This project provides the [slf4j](https://www.slf4j.org/) api and [logback](https://logback.qos.ch/) as a logging implementation. It includes basic [Sentry](https://sentry.io/) integration.

To configure Sentry, set `sentry.dsn` as a system property or `SENTRY_DSN` as an environment variable.

# Build
```
bazel //:logging
```

# Support
None.
