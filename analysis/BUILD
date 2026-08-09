load("@rules_cc//cc:defs.bzl", "cc_binary")

# Sims, not tests. Tests pin behaviour that must not change; each of these
# prints numbers to look at while deciding what the behaviour should be.
# They live outside //src so a tuning tool is never mistaken for the game.

cc_binary(
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

cc_binary(
    name = "weapon_sim",
    srcs = ["weapon_sim.cc"],
    deps = [
        "//src:embedded_data",
        "//src:game_state",
        "//src:proto_loader",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/combat:damage",
        "//src/combat:encounter",
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
    ],
)

cc_binary(
    name = "balance_sim",
    srcs = ["balance_sim.cc"],
    deps = [
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
    ],
)

cc_binary(
    name = "level_sim",
    srcs = ["level_sim.cc"],
    deps = [
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
    ],
)
