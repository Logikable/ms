load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

# The multiplayer server. It lives outside //src for the same reason the sims
# do: it is not the game a player runs.

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ids",
    srcs = ["ids.cc"],
    hdrs = ["ids.h"],
)

cc_library(
    name = "lobby",
    srcs = ["lobby.cc"],
    hdrs = ["lobby.h"],
    deps = [
        ":ids",
        "//src/protos:boss_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "lobby_test",
    srcs = ["lobby_test.cc"],
    deps = [
        ":lobby",
        "//src/protos:boss_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "server",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    deps = [
        ":ids",
        ":lobby",
        "//src/character",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_binary(
    name = "ms_server",
    srcs = ["main.cc"],
    deps = [
        ":server",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
    ],
)

# A headless client for driving a running server by hand. It lives here
# rather than beside the game because it is a tool for the server, not
# something a player runs.
cc_binary(
    name = "probe",
    srcs = ["probe.cc"],
    deps = [
        "//src/multiplayer:client",
        "//src/multiplayer:protocol",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    deps = [
        ":server",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)
