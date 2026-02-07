load("//bazel/rules:java.bzl", "artifact", "java_binary")

java_binary(
    name = "yochat",
    srcs = glob(["src/main/java/com/muchq/chat/yochat/*.java"]),
    main_class = "com.muchq.chat.yochat.App",
    visibility = ["//visibility:public"],
    deps = [
        "//domains/chat/libs/yochat_lib:lib",
        artifact("io.netty:netty-common"),
        artifact("io.netty:netty-transport"),
        artifact("org.slf4j:slf4j-api"),
    ],
)
