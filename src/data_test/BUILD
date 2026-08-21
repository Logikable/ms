load("@rules_cc//cc:defs.bzl", "cc_test")

# Checks on the shipped catalogs rather than on any one source file: that every
# textproto under data/ names something real, stays inside its own rules, and
# reads the way the panels that draw it expect. They live together because what
# they test is the data, not a package.

cc_test(
    name = "embedded_test",
    srcs = ["embedded_test.cc"],
    deps = [
        "//src:embedded_data",
        "//src:proto_loader",
        "//src/item",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "boss_test",
    srcs = ["boss_test.cc"],
    data = [
        "//data/bosses:all",
        "//data/equip:all",
        "//data/items:all",
        "//data/mobs:all",
    ],
    deps = [
        "//src:proto_loader",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "map_test",
    srcs = ["map_test.cc"],
    data = [
        "//data/equip:all",
        "//data/items:all",
        "//data/maps:all",
        "//data/mobs:all",
    ],
    deps = [
        "//src:proto_loader",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "workbench_gear_test",
    srcs = ["workbench_gear_test.cc"],
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
        "//src/character:exp_table",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "scroll_test",
    srcs = ["scroll_test.cc"],
    data = ["//data/scrolls:all"],
    deps = [
        "//src:proto_loader",
        "//src/item:spell_trace_cost",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "skill_test",
    srcs = ["skill_test.cc"],
    data = ["//data/skills:all"],
    deps = [
        "//src:proto_loader",
        "//src/character",
        "//src/combat:constants",
        "//src/combat:damage",
        "//src/frontend/widgets:panel_util",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "equip_test",
    srcs = ["equip_test.cc"],
    data = [
        "//data/equip:all",
        "//data/items:all",
        "//data/sets:all",
    ],
    deps = [
        "//src:proto_loader",
        "//src/character",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/item:projectile",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:skill_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
