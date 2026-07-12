load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "character_test",
    srcs = ["character_test.cc"],
    deps = [
        ":character",
        ":exp_table",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "exp_table_test",
    srcs = ["exp_table_test.cc"],
    deps = [
        ":exp_table",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "exp_table",
    srcs = ["exp_table.cc"],
    hdrs = ["exp_table.h"],
)

cc_library(
    name = "character",
    srcs = ["character.cc"],
    hdrs = ["character.h"],
    deps = [
        ":equip_instance",
        ":equip_stats",
        ":exp_table",
        ":inventory",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "inventory",
    srcs = ["inventory.cc"],
    hdrs = ["inventory.h"],
    deps = [
        ":equip_instance",
        ":item",
    ],
)

cc_test(
    name = "inventory_test",
    srcs = ["inventory_test.cc"],
    deps = [
        ":inventory",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

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
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
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
    ],
    deps = [
        ":equip_instance",
        ":game_state",
        ":item",
        ":proto_loader",
        "//src/frontend:tui",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
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

cc_library(
    name = "combat",
    srcs = ["combat.cc"],
    hdrs = ["combat.h"],
    deps = [
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "combat_test",
    srcs = ["combat_test.cc"],
    deps = [
        ":combat",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
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
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "farming",
    srcs = ["farming.cc"],
    hdrs = ["farming.h"],
    deps = [
        ":character",
        ":combat",
        ":equip_instance",
        ":game_state",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "farming_test",
    srcs = ["farming_test.cc"],
    deps = [
        ":equip_instance",
        ":farming",
        ":game_state",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "item",
    srcs = ["item.cc"],
    hdrs = ["item.h"],
    deps = [
        ":equip_stats",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_test(
    name = "item_test",
    srcs = ["item_test.cc"],
    deps = [
        ":item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equip_instance",
    srcs = ["equip_instance.cc"],
    hdrs = ["equip_instance.h"],
    deps = [
        ":equip_stats",
        ":item",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
    ],
)
