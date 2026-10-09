load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "skill_placement",
    srcs = ["skill_placement.cc"],
    hdrs = ["skill_placement.h"],
    deps = [
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "skill_placement_test",
    size = "small",
    srcs = ["skill_placement_test.cc"],
    deps = [
        ":skill_placement",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "character",
    srcs = ["character.cc"],
    hdrs = ["character.h"],
    deps = [
        ":consumables",
        ":equip_presets",
        ":exp_table",
        ":familiar",
        ":guild",
        ":hyper_stats",
        ":inner_ability",
        ":job_branch",
        ":legion",
        ":link",
        ":skill_placement",
        ":symbol",
        ":v_matrix",
        "//src/item",
        "//src/item:currency",
        "//src/item:equip_instance",
        "//src/item:equip_stats",
        "//src/item:flame",
        "//src/item:inventory",
        "//src/item:inventory_sort",
        "//src/item:projectile",
        "//src/item:ring_box",
        "//src/item:shop",
        "//src/item:soul",
        "//src/item:stack_tab",
        "//src/item:star_force_cost",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "dailies",
    srcs = ["dailies.cc"],
    hdrs = ["dailies.h"],
    deps = [
        ":boss_reset",
        ":character",
        ":symbol",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "dailies_test",
    size = "small",
    srcs = ["dailies_test.cc"],
    deps = [
        ":character",
        ":dailies",
        ":symbol",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "consumables",
    srcs = ["consumables.cc"],
    hdrs = ["consumables.h"],
    deps = [
        "//src/protos:character_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "consumables_test",
    size = "small",
    srcs = ["consumables_test.cc"],
    deps = [
        ":character",
        ":consumables",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "arcane_force",
    srcs = ["arcane_force.cc"],
    hdrs = ["arcane_force.h"],
)

cc_library(
    name = "symbol",
    srcs = ["symbol.cc"],
    hdrs = ["symbol.h"],
    deps = [
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "symbol_test",
    size = "small",
    srcs = ["symbol_test.cc"],
    deps = [
        ":symbol",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "arcane_force_test",
    size = "small",
    srcs = ["arcane_force_test.cc"],
    deps = [
        ":arcane_force",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sacred_power",
    srcs = ["sacred_power.cc"],
    hdrs = ["sacred_power.h"],
    deps = [":arcane_force"],
)

cc_test(
    name = "sacred_power_test",
    size = "small",
    srcs = ["sacred_power_test.cc"],
    deps = [
        ":sacred_power",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "familiar",
    srcs = ["familiar.cc"],
    hdrs = ["familiar.h"],
    deps = [
        ":stat_preset",
        "//src/combat:damage",
        "//src/item:potential",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "@abseil-cpp//absl/types:span",
        "@protobuf",
    ],
)

cc_test(
    name = "familiar_test",
    size = "small",
    srcs = ["familiar_test.cc"],
    deps = [
        ":familiar",
        ":stat_preset",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "honor",
    srcs = ["honor.cc"],
    hdrs = ["honor.h"],
    deps = [":inner_ability"],
)

cc_library(
    name = "v_matrix",
    srcs = ["v_matrix.cc"],
    hdrs = ["v_matrix.h"],
    deps = ["//src/protos:skill_proto_cc"],
)

cc_test(
    name = "v_matrix_test",
    size = "small",
    srcs = ["v_matrix_test.cc"],
    deps = [
        ":v_matrix",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "honor_test",
    size = "small",
    srcs = ["honor_test.cc"],
    deps = [
        ":honor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "stat_preset",
    hdrs = ["stat_preset.h"],
)

cc_library(
    name = "equip_presets",
    srcs = ["equip_presets.cc"],
    hdrs = ["equip_presets.h"],
    deps = [
        ":stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "equip_presets_test",
    size = "small",
    srcs = ["equip_presets_test.cc"],
    deps = [
        ":equip_presets",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "inner_ability",
    srcs = ["inner_ability.cc"],
    hdrs = ["inner_ability.h"],
    deps = [
        ":stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "inner_ability_test",
    size = "small",
    srcs = ["inner_ability_test.cc"],
    deps = [
        ":inner_ability",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hyper_stats",
    srcs = ["hyper_stats.cc"],
    hdrs = ["hyper_stats.h"],
    deps = [
        ":stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "hyper_stats_test",
    size = "small",
    srcs = ["hyper_stats_test.cc"],
    deps = [
        ":hyper_stats",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "job_name",
    srcs = ["job_name.cc"],
    hdrs = ["job_name.h"],
    deps = [
        ":hyper_stats",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "job_name_test",
    size = "small",
    srcs = ["job_name_test.cc"],
    deps = [
        ":job_name",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "job_branch",
    srcs = ["job_branch.cc"],
    hdrs = ["job_branch.h"],
    deps = ["//src/protos:character_proto_cc"],
)

cc_test(
    name = "job_branch_test",
    size = "small",
    srcs = ["job_branch_test.cc"],
    deps = [
        ":job_branch",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "guild",
    srcs = ["guild.cc"],
    hdrs = ["guild.h"],
    deps = [
        "//src/protos:account_proto_cc",
        "//src/protos:boss_proto_cc",
        "@protobuf",
    ],
)

cc_test(
    name = "guild_test",
    size = "small",
    srcs = ["guild_test.cc"],
    deps = [
        ":guild",
        "//src/protos:account_proto_cc",
        "//src/protos:boss_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "link",
    srcs = ["link.cc"],
    hdrs = ["link.h"],
    deps = [
        ":job_branch",
        ":stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "link_test",
    size = "small",
    srcs = ["link_test.cc"],
    deps = [
        ":link",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "legion",
    srcs = ["legion.cc"],
    hdrs = ["legion.h"],
    deps = [
        ":job_branch",
        ":stat_preset",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
    ],
)

cc_test(
    name = "legion_test",
    size = "small",
    srcs = ["legion_test.cc"],
    deps = [
        ":legion",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "legion_plan",
    srcs = ["legion_plan.cc"],
    hdrs = ["legion_plan.h"],
    deps = [
        ":character",
        ":legion",
        ":stat_preset",
        "//src/protos:legion_proto_cc",
    ],
)

cc_test(
    name = "legion_plan_test",
    size = "small",
    srcs = ["legion_plan_test.cc"],
    deps = [
        ":character",
        ":character_stats",
        ":legion",
        ":legion_plan",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_reset",
    srcs = ["boss_reset.cc"],
    hdrs = ["boss_reset.h"],
    deps = [
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "boss_reset_test",
    size = "small",
    srcs = ["boss_reset_test.cc"],
    deps = [
        ":boss_reset",
        ":character",
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "character_stats",
    srcs = ["character_stats.cc"],
    hdrs = ["character_stats.h"],
    deps = [
        ":character",
        ":consumables",
        ":familiar",
        ":hyper_stats",
        ":inner_ability",
        ":legion",
        ":skill_placement",
        ":symbol",
        "//src/combat:constants",
        "//src/combat:damage",
        "//src/item:equip_stats",
        "//src/item:flame",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:legion_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "progression",
    srcs = ["progression.cc"],
    hdrs = ["progression.h"],
    deps = [
        ":character",
        ":consumables",
        ":exp_table",
        ":familiar",
        ":guild",
        ":hyper_stats",
        ":link",
        ":sacred_power",
        "//src:account",
        "//src/item:flame",
        "//src/item:potential",
        "//src/item:soul",
        "//src/protos:character_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_test(
    name = "progression_test",
    size = "small",
    srcs = ["progression_test.cc"],
    deps = [
        ":character",
        ":exp_table",
        ":link",
        ":progression",
        "//src:account",
        "//src/item:potential",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "exp_table",
    srcs = ["exp_table.cc"],
    hdrs = ["exp_table.h"],
)

cc_library(
    name = "job_advancement",
    srcs = ["job_advancement.cc"],
    hdrs = ["job_advancement.h"],
    deps = [
        ":character",
        "//src:game_state",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "character_test",
    size = "small",
    srcs = ["character_test.cc"],
    deps = [
        ":character",
        ":equip_presets",
        ":exp_table",
        ":familiar",
        ":inner_ability",
        ":symbol",
        ":v_matrix",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:flame",
        "//src/item:potential",
        "//src/item:star_force_cost",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "character_stats_test",
    size = "small",
    srcs = ["character_stats_test.cc"],
    data = [
        "//data/equip:all",
        "//data/skills:all",
    ],
    deps = [
        ":character",
        ":character_stats",
        ":consumables",
        ":familiar",
        ":guild",
        ":hyper_stats",
        ":inner_ability",
        ":legion",
        ":symbol",
        ":v_matrix",
        "//src/combat:damage",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:familiar_proto_cc",
        "//src/protos:legion_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:data_files",
        "//src/testing:prototypes",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "exp_table_test",
    size = "small",
    srcs = ["exp_table_test.cc"],
    deps = [
        ":exp_table",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "job_advancement_test",
    size = "small",
    srcs = ["job_advancement_test.cc"],
    data = ["//data/equip:all"],
    deps = [
        ":job_advancement",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/testing:data_files",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hyper_plan",
    srcs = ["hyper_plan.cc"],
    hdrs = ["hyper_plan.h"],
    deps = [
        ":character",
        ":hyper_stats",
        ":stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "hyper_plan_test",
    srcs = ["hyper_plan_test.cc"],
    deps = [
        ":character",
        ":hyper_plan",
        ":stat_preset",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "noblesse_plan",
    srcs = ["noblesse_plan.cc"],
    hdrs = ["noblesse_plan.h"],
    deps = [
        ":character",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "noblesse_plan_test",
    size = "small",
    srcs = ["noblesse_plan_test.cc"],
    deps = [
        ":character",
        ":noblesse_plan",
        ":skill_placement",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "set_pieces",
    srcs = ["set_pieces.cc"],
    hdrs = ["set_pieces.h"],
    deps = [
        ":job_branch",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
    ],
)

cc_library(
    name = "max_character",
    srcs = ["max_character.cc"],
    hdrs = ["max_character.h"],
    deps = [
        ":character",
        ":character_stats",
        ":familiar",
        ":hyper_plan",
        ":hyper_stats",
        ":inner_ability",
        ":job_branch",
        ":legion_plan",
        ":noblesse_plan",
        ":set_pieces",
        ":stat_preset",
        "//src/combat:damage",
        "//src/item",
        "//src/item:flame",
        "//src/item:potential",
        "//src/item:soul",
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "max_character_test",
    size = "small",
    srcs = ["max_character_test.cc"],
    deps = [
        ":character",
        ":familiar",
        ":hyper_stats",
        ":inner_ability",
        ":max_character",
        "//src/item:flame",
        "//src/item:potential",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)
