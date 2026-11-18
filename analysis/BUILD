load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//analysis:__subpackages__"])

# Sims print numbers to read while tuning; tests pin behaviour that must not
# change. This package holds the tested libraries the sims share; the sims are
# in sims/ and the planners in plan/. It all lives outside //src so a tuning
# tool is never mistaken for the game.

cc_library(
    name = "sim_jobs",
    srcs = ["sim_jobs.cc"],
    hdrs = ["sim_jobs.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

proto_library(
    name = "sim_checkpoint_proto",
    srcs = ["sim_checkpoint.proto"],
    deps = [
        "//src/protos:character_proto",
        "//src/protos:equip_proto",
        "//src/protos:familiar_proto",
        "//src/protos:legion_proto",
    ],
)

cc_proto_library(
    name = "sim_checkpoint_proto_cc",
    deps = [":sim_checkpoint_proto"],
)

cc_library(
    name = "checkpoint",
    srcs = ["checkpoint.cc"],
    hdrs = ["checkpoint.h"],
    deps = [
        ":sim_checkpoint_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "drop_value",
    srcs = ["drop_value.cc"],
    hdrs = ["drop_value.h"],
    deps = [
        ":sim_gear",
        ":yardstick",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character/force:symbol",
        "//src/combat:damage",
        "//src/combat:loot",
        "//src/item:equip_instance",
        "//src/item:shop",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_library(
    name = "meso_rate",
    srcs = ["meso_rate.cc"],
    hdrs = ["meso_rate.h"],
    deps = [
        ":drop_value",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/combat:encounter",
        "//src/combat:loot",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "yardstick",
    srcs = ["yardstick.cc"],
    hdrs = ["yardstick.h"],
    deps = [
        ":sim_boss",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/combat:damage",
        "//src/combat:encounter",
        "//src/combat:measure",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "drop_value_test",
    size = "small",
    srcs = ["drop_value_test.cc"],
    deps = [
        ":drop_value",
        ":sim_gear",
        ":yardstick",
        "//src:game_state",
        "//src/character:skill_placement",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sim_gear_test",
    size = "small",
    srcs = ["sim_gear_test.cc"],
    deps = [
        ":sim_gear",
        "//src:game_state",
        "//src/character",
        "//src/character:progression",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "yardstick_test",
    size = "small",
    srcs = ["yardstick_test.cc"],
    deps = [
        ":yardstick",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character:skill_placement",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sim_boss",
    srcs = ["sim_boss.cc"],
    hdrs = ["sim_boss.h"],
    deps = [
        "//src:game_state",
        "//src:spawn",
        "//src/combat:boss_run",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "sim_boss_test",
    size = "small",
    srcs = ["sim_boss_test.cc"],
    deps = [
        ":sim_boss",
        "//src:game_state",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sim_gear",
    srcs = ["sim_gear.cc"],
    hdrs = ["sim_gear.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character:progression",
        "//src/character/force:symbol",
        "//src/combat:encounter",
        "//src/combat:measure",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:projectile",
        "//src/item:ring_box",
        "//src/item:shop",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_library(
    name = "sim_world",
    srcs = ["sim_world.cc"],
    hdrs = ["sim_world.h"],
    deps = [
        "//src:embedded_data",
        "//src:game_state",
        "//src:map_level",
        "//src:proto_loader",
        "//src/character",
        "//src/character/matrix:hexa_matrix",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_library(
    name = "parallel",
    srcs = ["parallel.cc"],
    hdrs = ["parallel.h"],
)

cc_library(
    name = "sim_format",
    srcs = ["sim_format.cc"],
    hdrs = ["sim_format.h"],
)

cc_library(
    name = "star_force_curve",
    srcs = ["star_force_curve.cc"],
    hdrs = ["star_force_curve.h"],
    deps = [
        "//src/item:equip_instance",
        "//src/item:star_force_cost",
    ],
)

cc_test(
    name = "star_force_curve_test",
    size = "small",
    srcs = ["star_force_curve_test.cc"],
    deps = [
        ":star_force_curve",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "reference_data",
    srcs = ["reference_data.cc"],
    hdrs = ["reference_data.h"],
    deps = [
        "//src/character:inner_ability",
        "//src/frontend/widgets:game_names",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:flame",
        "//src/item:potential",
        "//src/item:slot_order",
        "//src/item:star_force_cost",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "reference_data_test",
    size = "small",
    srcs = ["reference_data_test.cc"],
    deps = [
        ":reference_data",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
        "@protobuf//:json_util",
        "@protobuf//:struct_cc_proto",
    ],
)

cc_library(
    name = "run_stats",
    srcs = ["run_stats.cc"],
    hdrs = ["run_stats.h"],
    deps = ["@abseil-cpp//absl/strings"],
)

cc_test(
    name = "run_stats_test",
    size = "small",
    srcs = ["run_stats_test.cc"],
    deps = [
        ":run_stats",
        "@googletest//:gtest_main",
    ],
)
