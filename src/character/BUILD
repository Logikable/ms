load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "character",
    srcs = ["character.cc"],
    hdrs = ["character.h"],
    deps = [
        ":arcane_force",
        ":consumables",
        ":exp_table",
        ":hyper_stats",
        ":inner_ability",
        ":job_branch",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:equip_stats",
        "//src/item:inventory",
        "//src/item:projectile",
        "//src/item:star_force_cost",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/types:span",
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
    deps = [
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
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
    deps = ["//src/protos:character_proto_cc"],
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
        ":hyper_stats",
        ":inner_ability",
        "//src/combat:damage",
        "//src/item:equip_stats",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
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
        ":hyper_stats",
        "//src:account",
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
        ":exp_table",
        ":inner_ability",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:star_force_cost",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
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
        ":hyper_stats",
        ":inner_ability",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
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
