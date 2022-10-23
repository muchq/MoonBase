java_binary(
    name = "yochat",
    srcs = glob(["*.java"]),
    main_class = "com.muchq.yochat.App",
    deps = [
      "@maven//:io_netty_netty_common",
      "@maven//:io_netty_netty_handler",
      "@maven//:io_netty_netty_transport",
      "@maven//:io_netty_netty_codec",
      "@maven//:org_slf4j_slf4j_api",
      "@maven//:com_fasterxml_jackson_core_jackson_databind",
      "@maven//:com_fasterxml_jackson_datatype_jackson_datatype_jdk8",
      "@maven//:com_fasterxml_jackson_datatype_jackson_datatype_guava",
      "@maven//:com_fasterxml_jackson_datatype_jackson_datatype_jsr310",
    ],
    visibility = ["//visibility:public"],
)
