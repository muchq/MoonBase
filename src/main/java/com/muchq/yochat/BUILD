java_binary(
    name = "yochat",
    srcs = glob(["*.java"]),
    main_class = "com.muchq.yochat.App",
    deps = [
      "//src/main/java/com/muchq/yochat/lib",
      "@maven//:io_netty_netty_common",
      "@maven//:io_netty_netty_transport",
      "@maven//:org_slf4j_slf4j_api",
    ],
    visibility = ["//visibility:public"],
)
