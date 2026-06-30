load("//third_party/bazel_rules/rules_cc/cc:defs.bzl", "cc_binary", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "protocol",
    hdrs = ["protocol.h"],
)

cc_library(
    name = "flag_store",
    srcs = ["flag_store.cc"],
    hdrs = ["flag_store.h"],
    deps = [
        ":protocol",
    ],
)

cc_library(
    name = "thread_pool",
    srcs = ["thread_pool.cc"],
    hdrs = ["thread_pool.h"],
)

cc_library(
    name = "tcp_server",
    srcs = ["tcp_server.cc"],
    hdrs = ["tcp_server.h"],
    deps = [
        ":flag_store",
        ":thread_pool",
        ":protocol",
    ],
)

cc_library(
    name = "flag_client",
    srcs = ["flag_client.cc"],
    hdrs = ["flag_client.h"],
    deps = [
        ":flag_store",
        ":protocol",
    ],
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":flag_store",
        ":tcp_server",
    ],
)

cc_binary(
    name = "benchmark",
    srcs = ["benchmark.cc"],
    deps = [
        ":flag_store",
    ],
)

cc_binary(
    name = "client_main",
    srcs = ["client_main.cc"],
    deps = [
        ":flag_client",
    ],
)

cc_binary(
    name = "admin_cli",
    srcs = ["admin_cli.cc"],
    deps = [
        ":protocol",
    ],
)
