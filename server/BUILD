load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:release.bzl", "release_binary")

# The multiplayer server. It lives outside //src for the same reason the sims
# do: it is not the game a player runs.

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ids",
    srcs = ["ids.cc"],
    hdrs = ["ids.h"],
)

cc_library(
    name = "log_file",
    srcs = ["log_file.cc"],
    hdrs = ["log_file.h"],
    deps = [
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
    ],
)

cc_test(
    name = "log_file_test",
    size = "small",
    srcs = ["log_file_test.cc"],
    deps = [
        ":log_file",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "lobby",
    srcs = ["lobby.cc"],
    hdrs = ["lobby.h"],
    deps = [
        ":ids",
        "//src/character:boss_reset",
        "//src/multiplayer:protocol",
        "//src/protos:boss_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "fight",
    srcs = ["fight.cc"],
    hdrs = ["fight.h"],
    deps = [
        "//src:spawn",
        "//src/combat:boss_timing",
        "//src/combat:loot",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:multiplayer_proto_cc",
    ],
)

cc_test(
    name = "fight_test",
    size = "small",
    srcs = ["fight_test.cc"],
    deps = [
        ":fight",
        "//src/combat:boss_timing",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "lobby_test",
    size = "small",
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
        ":fight",
        ":ids",
        ":lobby",
        "//src/character",
        "//src/character:job_name",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "ms_server",
    srcs = ["main.cc"],
    deps = [
        ":log_file",
        ":server",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/log:log_sink_registry",
    ],
)

# The server on a loopback port, for tests on either side of the connection.
cc_library(
    name = "test_server",
    testonly = True,
    hdrs = ["test_server.h"],
    deps = [
        ":server",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
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

# What tools/deploy_server.sh sends: optimised and statically linked, so the
# box's glibc need not match the machine that built it.
release_binary(
    name = "ms_server_static",
    binary = ":ms_server",
    platform = "//release:linux_x86_64",
)

release_binary(
    name = "probe_static",
    binary = ":probe",
    platform = "//release:linux_x86_64",
)

cc_test(
    name = "server_test",
    size = "small",
    srcs = ["server_test.cc"],
    deps = [
        ":server",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/multiplayer:protocol",
        "//src/net:socket",
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@googletest//:gtest_main",
    ],
)
