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
        ":inner_ability",
        ":job_branch",
        ":skill_placement",
        "//src:build_config",
        "//src/character/account:familiar",
        "//src/character/account:guild",
        "//src/character/account:legion",
        "//src/character/account:link",
        "//src/character/force:symbol",
        "//src/character/matrix:hexa_matrix",
        "//src/character/matrix:hexa_stat",
        "//src/character/matrix:hyper_stats",
        "//src/character/matrix:v_matrix",
        "//src/item",
        "//src/item:currency",
        "//src/item:equip_instance",
        "//src/item:equip_stats",
        "//src/item:inventory",
        "//src/item:inventory_sort",
        "//src/item:projectile",
        "//src/item:ring_box",
        "//src/item:shop",
        "//src/item:stack_tab",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:soul",
        "//src/item/upgrade:star_force_cost",
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
        "//src/character/force:symbol",
        "//src/character/matrix:hyper_stats",
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
        "//src/character/force:symbol",
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
    name = "honor",
    srcs = ["honor.cc"],
    hdrs = ["honor.h"],
    deps = [":inner_ability"],
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
    name = "job_name",
    srcs = ["job_name.cc"],
    hdrs = ["job_name.h"],
    deps = [
        "//src/character/matrix:hyper_stats",
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
        ":inner_ability",
        ":skill_placement",
        "//src/character/account:familiar",
        "//src/character/account:legion",
        "//src/character/force:symbol",
        "//src/character/matrix:hexa_stat",
        "//src/character/matrix:hyper_stats",
        "//src/combat:constants",
        "//src/combat:damage",
        "//src/item:equip_stats",
        "//src/item/upgrade:flame",
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
        "//src:account",
        "//src/character/account:familiar",
        "//src/character/account:guild",
        "//src/character/account:legion",
        "//src/character/account:link",
        "//src/character/force:sacred_power",
        "//src/character/matrix:hyper_stats",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/item/upgrade:soul",
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
        ":progression",
        "//src:account",
        "//src/character/account:link",
        "//src/item/upgrade:potential",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "exp_table",
    srcs = ["exp_table.cc"],
    hdrs = ["exp_table.h"],
    deps = [
        "//src:build_config",
        "//src/protos:character_proto_cc",
    ],
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
        ":inner_ability",
        "//src/character/account:familiar",
        "//src/character/force:symbol",
        "//src/character/matrix:hexa_matrix",
        "//src/character/matrix:hexa_stat",
        "//src/character/matrix:sol_erda",
        "//src/character/matrix:v_matrix",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/item/upgrade:star_force_cost",
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
        ":inner_ability",
        "//src/character/account:familiar",
        "//src/character/account:guild",
        "//src/character/account:legion",
        "//src/character/force:symbol",
        "//src/character/matrix:hyper_stats",
        "//src/character/matrix:v_matrix",
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
        ":inner_ability",
        ":job_branch",
        ":set_pieces",
        ":stat_preset",
        "//src/character/account:familiar",
        "//src/character/account:legion_plan",
        "//src/character/account:noblesse_plan",
        "//src/character/matrix:hexa_matrix",
        "//src/character/matrix:hexa_stat",
        "//src/character/matrix:hyper_plan",
        "//src/character/matrix:hyper_stats",
        "//src/combat:damage",
        "//src/item",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/item/upgrade:soul",
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
        ":inner_ability",
        ":max_character",
        "//src/character/account:familiar",
        "//src/character/matrix:hyper_stats",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)
