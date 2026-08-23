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

cc_library(
    name = "client",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    deps = [
        ":protocol",
        "//src/net:socket",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

# The client against the real server: the one test that proves both ends of
# the protocol agree. It is the only thing under //src that depends on the
# server, and only because a test may.
cc_test(
    name = "client_test",
    srcs = ["client_test.cc"],
    deps = [
        ":client",
        "//server",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)
