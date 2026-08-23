load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "protocol",
    hdrs = ["protocol.h"],
    deps = ["//src/net:frame"],
)

cc_test(
    name = "protocol_test",
    srcs = ["protocol_test.cc"],
    deps = [
        ":protocol",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)
