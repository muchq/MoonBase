load("@dagger//:workspace_defs.bzl", "dagger_rules")

dagger_rules()

java_library(
    name = "logging",
    resources = ["config/logback.xml"],
    runtime_deps = [
      "@maven//:org_slf4j_slf4j_api",
      "@maven//:ch_qos_logback_logback_classic",
      "@maven//:io_sentry_sentry_logback",
    ],
)
