load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "character_panel",
    srcs = ["character_panel.cc"],
    hdrs = ["character_panel.h"],
    deps = [
        "//src/character",
        "//src/character:character_stats",
        "//src/combat:damage",
        "//src/frontend:types",
        "//src/frontend/widgets:panel_util",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "character_panel_test",
    srcs = ["character_panel_test.cc"],
    deps = [
        ":character_panel",
        "//src/frontend/widgets:panel_test_base",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equipped_panel",
    srcs = ["equipped_panel.cc"],
    hdrs = ["equipped_panel.h"],
    deps = [
        "//src/character",
        "//src/character:progression",
        "//src/frontend:types",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:panel_util",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "equipped_panel_test",
    srcs = ["equipped_panel_test.cc"],
    deps = [
        ":equipped_panel",
        "//src/frontend/widgets:panel_test_base",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "inventory_panel",
    srcs = ["inventory_panel.cc"],
    hdrs = ["inventory_panel.h"],
    deps = [
        "//src/character",
        "//src/character:progression",
        "//src/frontend:types",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "inventory_panel_test",
    srcs = ["inventory_panel_test.cc"],
    deps = [
        ":inventory_panel",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "combat_panel",
    srcs = ["combat_panel.cc"],
    hdrs = ["combat_panel.h"],
    deps = [
        ":character_panel",
        "//src:game_state",
        "//src/combat:fight",
        "//src/frontend:types",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_util",
        "//src/protos:map_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "combat_panel_test",
    srcs = ["combat_panel_test.cc"],
    deps = [
        ":combat_panel",
        "//src:game_state",
        "//src/combat:encounter",
        "//src/combat:fight",
        "//src/frontend:types",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
