load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "character_test",
    srcs = ["character_test.cc"],
    deps = [
        ":character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "character",
    srcs = ["character.cc"],
    hdrs = ["character.h"],
    deps = [
        ":equip_instance",
        ":equip_stats",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "proto_loader",
    srcs = ["proto_loader.cc"],
    hdrs = ["proto_loader.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@abseil-cpp//absl/log",
        "@protobuf",
    ],
)

cc_library(
    name = "game_state",
    srcs = ["game_state.cc"],
    hdrs = ["game_state.h"],
    deps = [
        ":character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
    ],
)

cc_binary(
    name = "ms",
    srcs = ["main.cc"],
    data = [
        "//data/equip:all",
        "//data/scrolls:all",
    ],
    deps = [
        ":game_state",
        ":proto_loader",
        "//src/frontend:tui",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)

cc_library(
    name = "equip_stats",
    srcs = ["equip_stats.cc"],
    hdrs = ["equip_stats.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "proto_loader_test",
    srcs = ["proto_loader_test.cc"],
    deps = [
        ":proto_loader",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "equip_stats_test",
    srcs = ["equip_stats_test.cc"],
    deps = [
        ":equip_stats",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "equip_instance_test",
    srcs = ["equip_instance_test.cc"],
    deps = [
        ":equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "game_state_test",
    srcs = ["game_state_test.cc"],
    deps = [
        ":game_state",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equip_instance",
    srcs = ["equip_instance.cc"],
    hdrs = ["equip_instance.h"],
    deps = [
        ":equip_stats",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
    ],
)
