load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//analysis:__subpackages__"])

cc_library(
    name = "ability_plan",
    srcs = ["ability_plan.cc"],
    hdrs = ["ability_plan.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character:inner_ability",
        "//src/protos:character_proto_cc",
    ],
)

cc_library(
    name = "hyper_plan",
    srcs = ["hyper_plan.cc"],
    hdrs = ["hyper_plan.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character:stat_preset",
        "//src/character/matrix:hyper_plan",
        "//src/protos:character_proto_cc",
    ],
)

cc_library(
    name = "legion_plan",
    srcs = ["legion_plan.cc"],
    hdrs = ["legion_plan.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character:stat_preset",
        "//src/character/account:legion",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
    ],
)

cc_test(
    name = "legion_plan_test",
    size = "small",
    srcs = ["legion_plan_test.cc"],
    deps = [
        ":legion_plan",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character/account:legion",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "buff_plan",
    srcs = ["buff_plan.cc"],
    hdrs = ["buff_plan.h"],
    deps = [
        "//analysis:meso_rate",
        "//src:game_state",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:consumables",
        "//src/combat:damage",
        "//src/combat:encounter",
        "//src/combat:loot",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "buff_plan_test",
    size = "small",
    srcs = ["buff_plan_test.cc"],
    deps = [
        ":buff_plan",
        "//analysis:meso_rate",
        "//src:game_state",
        "//src/character:consumables",
        "//src/protos:character_proto_cc",
        "//src/protos:mob_proto_cc",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "alt_plan",
    srcs = ["alt_plan.cc"],
    hdrs = ["alt_plan.h"],
    deps = [
        "//src/character:job_branch",
        "//src/character/account:link",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "alt_plan_test",
    size = "small",
    srcs = ["alt_plan_test.cc"],
    deps = [
        ":alt_plan",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "gain_distribution",
    srcs = ["gain_distribution.cc"],
    hdrs = ["gain_distribution.h"],
)

cc_test(
    name = "gain_distribution_test",
    size = "small",
    srcs = ["gain_distribution_test.cc"],
    deps = [
        ":gain_distribution",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "cube_plan",
    srcs = ["cube_plan.cc"],
    hdrs = ["cube_plan.h"],
    deps = [
        ":gain_distribution",
        "//analysis:sim_boss",
        "//analysis:sim_gear",
        "//analysis:yardstick",
        "//src:game_state",
        "//src/character",
        "//src/character:character_stats",
        "//src/combat:damage",
        "//src/item:equip_instance",
        "//src/item:equip_stats",
        "//src/item/upgrade:potential",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "flame_plan",
    srcs = ["flame_plan.cc"],
    hdrs = ["flame_plan.h"],
    deps = [
        ":cube_plan",
        ":gain_distribution",
        "//analysis:yardstick",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/item:equip_instance",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/protos:equip_proto_cc",
    ],
)

cc_library(
    name = "gear_plan",
    srcs = ["gear_plan.cc"],
    hdrs = ["gear_plan.h"],
    deps = [
        ":cube_plan",
        ":flame_plan",
        ":soul_plan",
        "//analysis:parallel",
        "//analysis:sim_gear",
        "//analysis:star_force_curve",
        "//analysis:yardstick",
        "//src:game_state",
        "//src:map_force",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/character/force:arcane_force",
        "//src/character/force:symbol",
        "//src/combat:damage",
        "//src/item",
        "//src/item:bank",
        "//src/item:currency",
        "//src/item:equip_instance",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/item/upgrade:soul",
        "//src/item/upgrade:spell_trace_cost",
        "//src/item/upgrade:star_force_cost",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
    ],
)

cc_library(
    name = "skill_plan",
    srcs = ["skill_plan.cc"],
    hdrs = ["skill_plan.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character/matrix:hexa_matrix",
        "//src/character/matrix:hexa_stat",
        "//src/character/matrix:sol_erda",
        "//src/character/matrix:v_matrix",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "gear_plan_test",
    size = "small",
    srcs = ["gear_plan_test.cc"],
    deps = [
        ":gear_plan",
        "//analysis:sim_gear",
        "//analysis:star_force_curve",
        "//src:game_state",
        "//src/character:progression",
        "//src/character:skill_placement",
        "//src/character/force:symbol",
        "//src/item:bank",
        "//src/item:equip_instance",
        "//src/item/upgrade:soul",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "familiar_plan",
    srcs = ["familiar_plan.cc"],
    hdrs = ["familiar_plan.h"],
    deps = [
        "//src:game_state",
        "//src/character:stat_preset",
        "//src/character/account:familiar",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
    ],
)

cc_test(
    name = "familiar_plan_test",
    size = "small",
    srcs = ["familiar_plan_test.cc"],
    deps = [
        ":familiar_plan",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character:skill_placement",
        "//src/character/account:familiar",
        "//src/protos:character_proto_cc",
        "//src/protos:familiar_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "soul_plan",
    srcs = ["soul_plan.cc"],
    hdrs = ["soul_plan.h"],
)

cc_test(
    name = "soul_plan_test",
    size = "small",
    srcs = ["soul_plan_test.cc"],
    deps = [
        ":soul_plan",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "flame_plan_test",
    size = "small",
    srcs = ["flame_plan_test.cc"],
    deps = [
        ":cube_plan",
        ":flame_plan",
        "//analysis:yardstick",
        "//src:game_state",
        "//src/character:skill_placement",
        "//src/item:equip_instance",
        "//src/item/upgrade:flame",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "cube_plan_test",
    size = "small",
    srcs = ["cube_plan_test.cc"],
    deps = [
        ":cube_plan",
        "//analysis:yardstick",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character:skill_placement",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "skill_plan_test",
    size = "small",
    srcs = ["skill_plan_test.cc"],
    deps = [
        ":skill_plan",
        "//src:build_config",
        "//src:game_state",
        "//src/character:skill_placement",
        "//src/character/matrix:hexa_matrix",
        "//src/character/matrix:hexa_stat",
        "//src/character/matrix:sol_erda",
        "//src/character/matrix:v_matrix",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)
