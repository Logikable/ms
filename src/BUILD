load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools:embed_data.bzl", "embedded_data")

package(default_visibility = ["//visibility:public"])

# The game's data files, compiled in. A shipped build has no data directory
# beside it: the executable is the whole game.
embedded_data(
    name = "embedded_data",
    groups = {
        "//data/bosses:all": "EmbeddedBosses",
        "//data/equip:all": "EmbeddedEquips",
        "//data/items:all": "EmbeddedItems",
        "//data/maps:all": "EmbeddedMaps",
        "//data/mobs:all": "EmbeddedMobs",
        "//data/scrolls:all": "EmbeddedScrolls",
        "//data/sets:all": "EmbeddedSets",
        "//data/skills:all": "EmbeddedSkills",
    },
)

# The one switch a single-player build is made with:
#
#   bazelisk build --define=multiplayer=off //src:ms
#
# Every multiplayer entry point asks kMultiplayerEnabled, so a build made this
# way has no party screens and opens no connection.
config_setting(
    name = "multiplayer_off",
    values = {"define": "multiplayer=off"},
)

cc_library(
    name = "build_config",
    hdrs = ["build_config.h"],
    defines = select({
        ":multiplayer_off": ["MS_MULTIPLAYER_OFF"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "proto_loader",
    srcs = ["proto_loader.cc"],
    hdrs = ["proto_loader.h"],
    deps = [
        "//src/protos:boss_proto_cc",
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
    name = "spawn",
    hdrs = ["spawn.h"],
    deps = ["//src/protos:mob_proto_cc"],
)

cc_library(
    name = "map_level",
    srcs = ["map_level.cc"],
    hdrs = ["map_level.h"],
    deps = [
        "//src:spawn",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "spawn_test",
    srcs = ["spawn_test.cc"],
    deps = [
        ":spawn",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "map_level_test",
    srcs = ["map_level_test.cc"],
    deps = [
        ":map_level",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "account",
    srcs = ["account.cc"],
    hdrs = ["account.h"],
    deps = [
        "//src/protos:account_proto_cc",
        "//src/protos:keybinds_proto_cc",
    ],
)

cc_test(
    name = "account_test",
    srcs = ["account_test.cc"],
    deps = [
        ":account",
        "//src/protos:account_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "save_migration",
    srcs = ["save_migration.cc"],
    hdrs = ["save_migration.h"],
    deps = [
        "//src/protos:account_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:save_proto_cc",
        "@protobuf",
    ],
)

cc_library(
    name = "save",
    srcs = ["save.cc"],
    hdrs = ["save.h"],
    deps = [
        ":account",
        ":game_state",
        ":save_migration",
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
        "//src/character",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:keybinds_proto_cc",
        "//src/protos:save_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "game_state",
    srcs = ["game_state.cc"],
    hdrs = ["game_state.h"],
    deps = [
        ":account",
        "//src/character",
        "//src/character:arcane_force",
        "//src/character:exp_table",
        "//src/character:job_name",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:equip_stats",
        "//src/item:inventory",
        "//src/protos:boss_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:save_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

_MAIN_DEPS = [
    ":build_config",
    ":embedded_data",
    ":save",
    ":game_state",
    ":proto_loader",
    "//src/character:exp_table",
    "//src/combat:offline",
    "//src/frontend:tui",
    "//src/item",
    "//src/item:equip_instance",
    "//src/multiplayer:protocol",
    "//src/protos:boss_proto_cc",
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
        "//src/character:arcane_force",
        "//src/character:exp_table",
        "//src/character:progression",
        "//src/item",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)
