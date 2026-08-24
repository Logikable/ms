load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "protocol",
    hdrs = ["protocol.h"],
    deps = [
        "//src:build_config",
        "//src/net:frame",
    ],
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
        "//server:test_server",
        "//src/net:socket",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "session",
    srcs = ["session.cc"],
    hdrs = ["session.h"],
    deps = [
        ":client",
        "//src:game_state",
        "//src/character",
        "//src/protos:multiplayer_proto_cc",
        "@protobuf",
    ],
)

cc_test(
    name = "session_test",
    srcs = ["session_test.cc"],
    deps = [
        ":session",
        "//server:test_server",
        "//src:game_state",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)
