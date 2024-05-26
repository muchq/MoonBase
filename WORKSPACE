workspace(name = "moon_base")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:http_archives.bzl", "register_http_archive_dependencies")

register_http_archive_dependencies()

load("@bazel_features//:deps.bzl", "bazel_features_deps")

bazel_features_deps()

#######################################################################################
##################################################
##################
#
#                             load protos first to get pre-compiled version
#
#############################################################
#######################################################################################

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies")

rules_proto_dependencies()

load("@rules_proto//proto:toolchains.bzl", "rules_proto_toolchains")

rules_proto_toolchains()

########################################################################################
##################################################
###################
#
#                             go stuff
#
##############################################################
########################################################################################

load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")

go_rules_dependencies()

go_register_toolchains(version = "1.22.2")

########################################################################################
##################################################
###################
#
#                             grpc proto stuff
#
##############################################################
########################################################################################

load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_repos", "rules_proto_grpc_toolchains")

rules_proto_grpc_toolchains()

rules_proto_grpc_repos()

load("@rules_proto_grpc//cpp:repositories.bzl", "cpp_repos")

cpp_repos()

########################################################################################
##################################################
###################
#
#                             cpp stuff
#
##############################################################
########################################################################################

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

load("@hedron_compile_commands//:workspace_setup.bzl", "hedron_compile_commands_setup")

hedron_compile_commands_setup()

########################################################################################
##################################################
###################
#
#                              java stuff
#
##############################################################
########################################################################################

load("@rules_jvm_external//:repositories.bzl", "rules_jvm_external_deps")

rules_jvm_external_deps()

load("@rules_jvm_external//:setup.bzl", "rules_jvm_external_setup")

rules_jvm_external_setup()

load("@rules_jvm_external//:defs.bzl", "maven_install")

maven_install(
    artifacts = [
        "ch.qos.logback:logback-classic:1.4.5",
        "ch.qos.logback:logback-core:1.4.5",
        "com.fasterxml.jackson.core:jackson-annotations:2.17.1",
        "com.fasterxml.jackson.core:jackson-core:2.13.4",
        "com.fasterxml.jackson.core:jackson-databind:2.13.4",
        "com.fasterxml.jackson.datatype:jackson-datatype-guava:2.13.4",
        "com.fasterxml.jackson.datatype:jackson-datatype-jdk8:2.13.4",
        "com.fasterxml.jackson.datatype:jackson-datatype-jsr310:2.13.4",
        "com.fasterxml.jackson.module:jackson-module-scala_2.13:2.13.4",
        "com.google.guava:guava:31.1-jre",
        "io.netty:netty-common:4.1.110.Final",
        "io.netty:netty-codec:4.1.110.Final",
        "io.netty:netty-handler:4.1.110.Final",
        "io.netty:netty-transport:4.1.110.Final",
        "io.sentry:sentry-logback:6.4.2",
        "junit:junit:4.13.2",
        "org.apache.spark:spark-core_2.13:3.3.1",
        "org.apache.spark:spark-sql_2.13:3.3.1",
        "org.apache.spark:spark-tags_2.13:3.3.1",
        "org.assertj:assertj-core:3.23.1",
        "org.scala-lang:scala3-library_3:jar:3.3.0",
        "org.scala-lang:scala3-compiler_3:3.3.0",
        "org.slf4j:slf4j-api:2.0.13",
    ],
    repositories = [
        "https://maven.google.com",
        "https://repo1.maven.org/maven2",
    ],
)

# switch back to bazel-deps once scala3 support is added
# https://github.com/bazeltools/bazel-deps/issues/326
#load("//3rdparty:workspace.bzl", "maven_dependencies")
#
#maven_dependencies()
#
#load("//3rdparty:target_file.bzl", "build_external_workspace")
#
#build_external_workspace(name = "third_party")

load("@contrib_rules_jvm//:repositories.bzl", "contrib_rules_jvm_deps")

contrib_rules_jvm_deps()

load("@contrib_rules_jvm//:setup.bzl", "contrib_rules_jvm_setup")

contrib_rules_jvm_setup()

########################################################################################
##################################################
###################
#
#                             scala stuff
#
##############################################################
########################################################################################

bind(
    name = "io_bazel_rules_scala/dependency/scalatest/scalatest",
    actual = "//3rdparty/jvm/org/scalatest",
)

load("@io_bazel_rules_scala//:scala_config.bzl", "scala_config")

scala_config(scala_version = "3.3.1")

load("@io_bazel_rules_scala//scala:scala.bzl", "scala_repositories")

scala_repositories()

load("@io_bazel_rules_scala//scala:toolchains.bzl", "scala_register_toolchains")

scala_register_toolchains()

load("@io_bazel_rules_scala//testing:scalatest.bzl", "scalatest_repositories", "scalatest_toolchain")

scalatest_repositories()

scalatest_toolchain()

register_toolchains("//:diagnostics_reporter_toolchain")

load("@io_bazel_rules_scala//scala/scalafmt:scalafmt_repositories.bzl", "scalafmt_default_config", "scalafmt_repositories")

scalafmt_default_config()

scalafmt_repositories()
