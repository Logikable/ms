load("@rules_cc//cc:defs.bzl", "cc_library")
load(":sim.bzl", "sim_binary")

# Sims, not tests. Tests pin behaviour that must not change; each of these
# prints numbers to look at while deciding what the behaviour should be.
# They live outside //src so a tuning tool is never mistaken for the game.

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
    name = "gear_plan",
    srcs = ["gear_plan.cc"],
    hdrs = ["gear_plan.h"],
    deps = [
        ":sim_gear",
        ":star_force_curve",
        "//src:game_state",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/combat:damage",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:spell_trace_cost",
        "//src/item:star_force_cost",
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
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
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

sim_binary(
    name = "ability_sim",
    srcs = ["ability_sim.cc"],
    deps = [
        "//src/character:inner_ability",
        "//src/protos:character_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

sim_binary(
    name = "weapon_sim",
    srcs = ["weapon_sim.cc"],
    deps = [
        ":sim_gear",
        ":sim_jobs",
        ":sim_world",
        "//src:embedded_data",
        "//src:game_state",
        "//src:proto_loader",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/combat:damage",
        "//src/combat:encounter",
        "//src/item:equip_instance",
        "//src/item:projectile",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
    ],
)

sim_binary(
    name = "balance_sim",
    srcs = ["balance_sim.cc"],
    deps = [
        ":parallel",
        ":sim_format",
        ":sim_gear",
        ":sim_jobs",
        ":sim_world",
        "//src:embedded_data",
        "//src:game_state",
        "//src:map_level",
        "//src:proto_loader",
        "//src/character",
        "//src/character:progression",
        "//src/combat",
        "//src/combat:encounter",
        "//src/combat:fight",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

sim_binary(
    name = "progression_sim",
    srcs = ["progression_sim.cc"],
    deps = [
        ":ability_plan",
        ":gear_plan",
        ":parallel",
        ":sim_boss",
        ":sim_format",
        ":sim_gear",
        ":sim_jobs",
        ":sim_world",
        ":skill_plan",
        "//src:embedded_data",
        "//src:game_state",
        "//src:proto_loader",
        "//src/character",
        "//src/character:exp_table",
        "//src/character:honor",
        "//src/character:inner_ability",
        "//src/character:job_advancement",
        "//src/character:progression",
        "//src/combat",
        "//src/combat:encounter",
        "//src/combat:fight",
        "//src/combat:loot",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:shop",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

sim_binary(
    name = "mob_curve_sim",
    srcs = ["mob_curve_sim.cc"],
    deps = [
        "//src:embedded_data",
        "//src:proto_loader",
        "//src:spawn",
        "//src/combat:constants",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
    ],
)

cc_library(
    name = "gms_mob_exp",
    srcs = ["gms_mob_exp.cc"],
    hdrs = ["gms_mob_exp.h"],
)

cc_library(
    name = "sim_gear",
    srcs = ["sim_gear.cc"],
    hdrs = ["sim_gear.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character:arcane_force",
        "//src/character:progression",
        "//src/combat:encounter",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:projectile",
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
    deps = [
        "//src/character:exp_table",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "meso_curve",
    srcs = ["meso_curve.cc"],
    hdrs = ["meso_curve.h"],
    deps = [
        ":gms_mob_exp",
        "//src/character:exp_table",
        "//src/combat:loot",
        "//src/protos:mob_proto_cc",
    ],
)

sim_binary(
    name = "economy_sim",
    srcs = ["economy_sim.cc"],
    deps = [
        ":meso_curve",
        ":sim_format",
        ":star_force_curve",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/character:exp_table",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:spell_trace_cost",
        "//src/item:star_force_cost",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
    ],
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
