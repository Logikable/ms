load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

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
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
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
        "//src:character",
        "//src:game_state",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "combat_test",
    srcs = ["combat_test.cc"],
    deps = [
        ":combat",
        ":fight",
        "//src:equip_instance",
        "//src:game_state",
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
        "//src:character",
        "//src:equip_instance",
        "//src:game_state",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "encounter_test",
    srcs = ["encounter_test.cc"],
    deps = [
        ":constants",
        ":encounter",
        "//src:equip_instance",
        "//src:game_state",
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

cc_library(
    name = "idle_rate",
    srcs = ["idle_rate.cc"],
    hdrs = ["idle_rate.h"],
    deps = [
        ":constants",
        ":damage",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "idle_rate_test",
    srcs = ["idle_rate_test.cc"],
    deps = [
        ":constants",
        ":damage",
        ":idle_rate",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)
