load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

# The multiplayer server. It lives outside //src for the same reason the sims
# do: it is not the game a player runs.

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "server",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    deps = [
        "//src/character",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_binary(
    name = "ms_server",
    srcs = ["main.cc"],
    deps = [
        ":server",
        "//src/multiplayer:protocol",
        "//src/net:socket",
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
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)
