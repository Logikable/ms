load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "constants",
    hdrs = ["constants.h"],
)

cc_library(
    name = "damage",
    srcs = ["damage.cc"],
    hdrs = ["damage.h"],
    deps = [
        ":constants",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "damage_test",
    srcs = ["damage_test.cc"],
    deps = [
        ":damage",
        "//src/character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "battle_analysis",
    srcs = ["battle_analysis.cc"],
    hdrs = ["battle_analysis.h"],
)

cc_test(
    name = "battle_analysis_test",
    srcs = ["battle_analysis_test.cc"],
    deps = [
        ":battle_analysis",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "loot",
    srcs = ["loot.cc"],
    hdrs = ["loot.h"],
    deps = ["//src/protos:mob_proto_cc"],
)

cc_test(
    name = "loot_test",
    srcs = ["loot_test.cc"],
    deps = [
        ":loot",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "combat",
    srcs = ["combat.cc"],
    hdrs = ["combat.h"],
    deps = [
        ":encounter",
        ":fight",
        ":loot",
        "//src:game_state",
        "//src/character",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "combat_test",
    srcs = ["combat_test.cc"],
    deps = [
        ":combat",
        ":encounter",
        ":fight",
        ":loot",
        "//src:game_state",
        "//src/character:exp_table",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_timing",
    hdrs = ["boss_timing.h"],
)

cc_library(
    name = "boss_run",
    srcs = ["boss_run.cc"],
    hdrs = ["boss_run.h"],
    deps = [
        ":boss_timing",
        ":combat",
        ":encounter",
        ":fight",
        ":loot",
        "//src:game_state",
        "//src/character",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "boss_run_test",
    srcs = ["boss_run_test.cc"],
    deps = [
        ":boss_run",
        ":encounter",
        "//src:game_state",
        "//src/character",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "offline",
    srcs = ["offline.cc"],
    hdrs = ["offline.h"],
    deps = [
        ":combat",
        ":encounter",
        ":fight",
        "//src:game_state",
        "//src/character",
        "//src/protos:map_proto_cc",
    ],
)

cc_test(
    name = "offline_test",
    srcs = ["offline_test.cc"],
    deps = [
        ":combat",
        ":encounter",
        ":offline",
        "//src:game_state",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "encounter",
    srcs = ["encounter.cc"],
    hdrs = ["encounter.h"],
    deps = [
        ":constants",
        ":damage",
        "//src:game_state",
        "//src:spawn",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "@protobuf",
    ],
)

cc_test(
    name = "encounter_test",
    srcs = ["encounter_test.cc"],
    deps = [
        ":constants",
        ":encounter",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "fight",
    srcs = ["fight.cc"],
    hdrs = ["fight.h"],
    deps = [
        ":encounter",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "fight_test",
    srcs = ["fight_test.cc"],
    deps = [
        ":encounter",
        ":fight",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)
