load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

RULES_JVM_EXTERNAL_TAG = "4.4.2"
RULES_JVM_EXTERNAL_SHA = "735602f50813eb2ea93ca3f5e43b1959bd80b213b836a07a62a29d757670b77b"

http_archive(
    name = "rules_jvm_external",
    strip_prefix = "rules_jvm_external-%s" % RULES_JVM_EXTERNAL_TAG,
    sha256 = RULES_JVM_EXTERNAL_SHA,
    url = "https://github.com/bazelbuild/rules_jvm_external/archive/refs/tags/%s.zip" % RULES_JVM_EXTERNAL_TAG,
)

DAGGER_TAG = "2.44"
DAGGER_SHA = "8c0876d46e8ce9332c4d4fbc2444420e0d75f041b3d4bab8313d2542d1e758ff"
http_archive(
    name = "dagger",
    strip_prefix = "dagger-dagger-%s" % DAGGER_TAG,
    sha256 = DAGGER_SHA,
    urls = ["https://github.com/google/dagger/archive/dagger-%s.zip" % DAGGER_TAG],
)

load("@rules_jvm_external//:repositories.bzl", "rules_jvm_external_deps")

rules_jvm_external_deps()

load("@rules_jvm_external//:setup.bzl", "rules_jvm_external_setup")

rules_jvm_external_setup()

load("@rules_jvm_external//:defs.bzl", "maven_install")
load("@dagger//:workspace_defs.bzl", "DAGGER_ARTIFACTS", "DAGGER_REPOSITORIES")

ASSERTJ_VERSION = "3.23.1"
GUAVA_VERSION = "31.1-jre"
JACKSON_VERSION = "2.13.4"
JUNIT4_VERSION = "4.13.2"
LOGBACK_VERSION = "1.4.3"
NETTY_VERSION = "4.1.84.Final"
RESTEASY_VERSION = "6.2.0.Final"
SENTRY_VERSION = "6.4.2"
SLF4J_VERSION = "2.0.3"

maven_install(
    artifacts = DAGGER_ARTIFACTS + [
        "org.assertj:assertj-core:%s" % ASSERTJ_VERSION,
        "org.slf4j:slf4j-api:%s" % SLF4J_VERSION,
        "ch.qos.logback:logback-classic:%s" % LOGBACK_VERSION,
        "io.sentry:sentry-logback:%s" % SENTRY_VERSION,
        "junit:junit:%s" % JUNIT4_VERSION,
        "com.google.guava:guava:%s" % GUAVA_VERSION,
        "io.netty:netty-all:%s" % NETTY_VERSION,
        "com.fasterxml.jackson.core:jackson-core:%s" % JACKSON_VERSION,
        "com.fasterxml.jackson.core:jackson-databind:%s" % JACKSON_VERSION,
        "com.fasterxml.jackson.datatype:jackson-datatype-jdk8:%s" % JACKSON_VERSION,
        "com.fasterxml.jackson.datatype:jackson-datatype-guava:%s" % JACKSON_VERSION,
        "com.fasterxml.jackson.datatype:jackson-datatype-jsr310:%s" % JACKSON_VERSION,
        "javax.inject:javax.inject:1",
    ],
    repositories = DAGGER_REPOSITORIES + [
        "https://maven.google.com",
        "https://repo1.maven.org/maven2",
    ],
)
