java_library(
    name = "logging",
    resources = ["config/logback.xml"],
    runtime_deps = [
        "@third_party//ch/qos/logback:logback_classic",
        "@third_party//io/sentry:sentry_logback",
        "@third_party//org/slf4j:slf4j_api",
    ],
)

alias(
    name = "go-images",
    actual = "//src/main/golang/images",
    visibility = ["//visibility:public"],
)
