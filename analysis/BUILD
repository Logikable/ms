load("@rules_cc//cc:defs.bzl", "cc_library")
load(":sim.bzl", "sim_binary")

# Sims, not tests. Tests pin behaviour that must not change; each of these
# prints numbers to look at while deciding what the behaviour should be.
# They live outside //src so a tuning tool is never mistaken for the game.

sim_binary(
    name = "meso_sim",
    srcs = ["meso_sim.cc"],
    data = [
        "//data/equip:all",
        "//data/items:all",
        "//data/maps:all",
        "//data/mobs:all",
        "//data/scrolls:all",
        "//data/skills:all",
    ],
    deps = [
        "//src:game_state",
        "//src:proto_loader",
        "//src/character",
        "//src/combat",
        "//src/combat:encounter",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/log",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)

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

sim_binary(
    name = "boss_sim",
    srcs = ["boss_sim.cc"],
    deps = [
        ":parallel",
        ":sim_gear",
        ":sim_jobs",
        "//src:embedded_data",
        "//src:game_state",
        "//src:proto_loader",
        "//src/character",
        "//src/character:character_stats",
        "//src/combat:boss_run",
        "//src/combat:encounter",
        "//src/protos:boss_proto_cc",
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
    name = "weapon_sim",
    srcs = ["weapon_sim.cc"],
    deps = [
        ":sim_gear",
        ":sim_jobs",
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
        "//src:embedded_data",
        "//src:game_state",
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
    name = "level_sim",
    srcs = ["level_sim.cc"],
    deps = [
        ":parallel",
        ":sim_format",
        ":sim_gear",
        ":sim_jobs",
        "//src:embedded_data",
        "//src:game_state",
        "//src:proto_loader",
        "//src/character",
        "//src/character:exp_table",
        "//src/character:job_advancement",
        "//src/character:progression",
        "//src/combat",
        "//src/combat:encounter",
        "//src/combat:fight",
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

sim_binary(
    name = "meso_curve_sim",
    srcs = ["meso_curve_sim.cc"],
    deps = [
        ":meso_curve",
        ":sim_format",
        "//src/character:exp_table",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
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
        "//src/combat:encounter",
        "//src/item:equip_instance",
        "//src/item:projectile",
        "//src/item:shop",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
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
    name = "scroll_cost_sim",
    srcs = ["scroll_cost_sim.cc"],
    deps = [
        ":meso_curve",
        ":sim_format",
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/item:equip_instance",
        "//src/item:spell_trace_cost",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
    ],
)
