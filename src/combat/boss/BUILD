load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "battle_analysis",
    srcs = ["battle_analysis.cc"],
    hdrs = ["battle_analysis.h"],
)

cc_test(
    name = "battle_analysis_test",
    size = "small",
    srcs = ["battle_analysis_test.cc"],
    deps = [
        ":battle_analysis",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_timing",
    hdrs = ["boss_timing.h"],
    deps = ["//src/combat:constants"],
)

cc_library(
    name = "arena_spots",
    srcs = ["arena_spots.cc"],
    hdrs = ["arena_spots.h"],
    deps = [
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "arena_spots_test",
    size = "small",
    srcs = ["arena_spots_test.cc"],
    deps = [
        ":arena_spots",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_run",
    srcs = ["boss_run.cc"],
    hdrs = ["boss_run.h"],
    deps = [
        ":arena_spots",
        ":boss_timing",
        "//src:build_config",
        "//src:game_state",
        "//src:spawn",
        "//src/character",
        "//src/character:honor",
        "//src/character/account:guild",
        "//src/character/matrix:v_matrix",
        "//src/combat",
        "//src/combat:constants",
        "//src/combat:damage_breakdown",
        "//src/combat:drop",
        "//src/combat:encounter",
        "//src/combat:fight",
        "//src/combat:fight_authority",
        "//src/combat:loot",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:multiplayer_proto_cc",
    ],
)

cc_test(
    name = "boss_run_test",
    size = "small",
    srcs = ["boss_run_test.cc"],
    deps = [
        ":boss_run",
        "//src:build_config",
        "//src:game_state",
        "//src/character",
        "//src/character:honor",
        "//src/character:skill_placement",
        "//src/character/matrix:hexa_matrix",
        "//src/character/matrix:hyper_stats",
        "//src/character/matrix:v_matrix",
        "//src/combat:damage_breakdown",
        "//src/combat:encounter",
        "//src/combat:test_authority",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)
