load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "proto_loader",
    srcs = ["proto_loader.cc"],
    hdrs = ["proto_loader.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/log",
        "@protobuf",
    ],
)

cc_library(
    name = "game_state",
    srcs = ["game_state.cc"],
    hdrs = ["game_state.h"],
    deps = [
        "//src/character",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_binary(
    name = "ms",
    srcs = ["main.cc"],
    data = [
        "//data/equip:all",
        "//data/items:all",
        "//data/maps:all",
        "//data/mobs:all",
        "//data/scrolls:all",
        "//data/skills:all",
    ],
    deps = [
        ":game_state",
        ":proto_loader",
        "//src/frontend:tui",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)

cc_test(
    name = "skill_data_test",
    srcs = ["skill_data_test.cc"],
    data = ["//data/skills:all"],
    deps = [
        ":proto_loader",
        "//src/protos:skill_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "proto_loader_test",
    srcs = ["proto_loader_test.cc"],
    deps = [
        ":proto_loader",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "game_state_test",
    srcs = ["game_state_test.cc"],
    deps = [
        ":game_state",
        "//src/character:progression",
        "//src/item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)
