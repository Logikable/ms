load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:embed_data.bzl", "embedded_data")

package(default_visibility = ["//visibility:public"])

# The game's data files, compiled in. A shipped build has no data directory
# beside it: the executable is the whole game.
embedded_data(
    name = "embedded_data",
    groups = {
        "//data/equip:all": "EmbeddedEquips",
        "//data/items:all": "EmbeddedItems",
        "//data/maps:all": "EmbeddedMaps",
        "//data/mobs:all": "EmbeddedMobs",
        "//data/scrolls:all": "EmbeddedScrolls",
        "//data/sets:all": "EmbeddedSets",
        "//data/skills:all": "EmbeddedSkills",
    },
)

cc_library(
    name = "proto_loader",
    srcs = ["proto_loader.cc"],
    hdrs = ["proto_loader.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/log",
        "@protobuf",
    ],
)

cc_library(
    name = "save",
    srcs = ["save.cc"],
    hdrs = ["save.h"],
    deps = [
        ":game_state",
        "//src/protos:save_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_test(
    name = "save_test",
    srcs = ["save_test.cc"],
    deps = [
        ":game_state",
        ":save",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:save_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "game_state",
    srcs = ["game_state.cc"],
    hdrs = ["game_state.h"],
    deps = [
        "//src/character",
        "//src/character:exp_table",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

_MAIN_DEPS = [
    ":embedded_data",
    ":save",
    ":game_state",
    ":proto_loader",
    "//src/frontend:tui",
    "//src/item",
    "//src/item:equip_instance",
    "//src/protos:character_proto_cc",
    "//src/protos:equip_proto_cc",
    "//src/protos:equip_set_proto_cc",
    "//src/protos:item_proto_cc",
    "//src/protos:map_proto_cc",
    "//src/protos:mob_proto_cc",
    "//src/protos:scroll_proto_cc",
    "//src/protos:skill_proto_cc",
    "@abseil-cpp//absl/flags:flag",
    "@abseil-cpp//absl/flags:parse",
    "@abseil-cpp//absl/log",
    "@abseil-cpp//absl/strings",
]

cc_binary(
    name = "ms",
    srcs = ["main.cc"],
    deps = _MAIN_DEPS,
)

# The same game, named for Windows. It needs its own target because the name
# has to carry .exe: the mingw driver appends that to any output without an
# extension, and Bazel would then go looking for a file nothing wrote.
cc_binary(
    name = "ms.exe",
    srcs = ["main.cc"],
    target_compatible_with = ["@platforms//os:windows"],
    deps = _MAIN_DEPS,
)

cc_test(
    name = "proto_loader_test",
    srcs = ["proto_loader_test.cc"],
    deps = [
        ":proto_loader",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "game_state_test",
    srcs = ["game_state_test.cc"],
    deps = [
        ":game_state",
        "//src/character",
        "//src/character:exp_table",
        "//src/character:progression",
        "//src/item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)
