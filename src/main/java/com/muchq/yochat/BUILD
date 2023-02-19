java_binary(
    name = "yochat",
    srcs = glob(["*.java"]),
    main_class = "com.muchq.yochat.App",
    visibility = ["//visibility:public"],
    deps = [
        "//src/main/java/com/muchq/yochat/lib",
        "@third_party//io/netty:netty_common",
        "@third_party//io/netty:netty_transport",
        "@third_party//org/slf4j:slf4j_api",
    ],
)
