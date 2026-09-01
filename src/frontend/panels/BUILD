load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "character_panel",
    srcs = ["character_panel.cc"],
    hdrs = ["character_panel.h"],
    deps = [
        "//src/character",
        "//src/character:character_stats",
        "//src/character:consumables",
        "//src/character:job_name",
        "//src/character:progression",
        "//src/combat:damage",
        "//src/frontend:panel_widths",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:stat_rows",
        "//src/frontend/widgets:text_columns",
        "//src/frontend/widgets:text_field",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "character_panel_test",
    size = "small",
    srcs = ["character_panel_test.cc"],
    deps = [
        ":character_panel",
        "//src/character:consumables",
        "//src/frontend:panel_widths",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:screen_text",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equipped_panel",
    srcs = ["equipped_panel.cc"],
    hdrs = ["equipped_panel.h"],
    deps = [
        "//src/character",
        "//src/character:arcane_force",
        "//src/character:progression",
        "//src/frontend:panel_widths",
        "//src/frontend:types",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:equipped_list",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:item_row",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "equipped_panel_test",
    size = "small",
    srcs = ["equipped_panel_test.cc"],
    deps = [
        ":equipped_panel",
        "//src/character:progression",
        "//src/frontend:panel_widths",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:keys",
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
        "//src/character:arcane_force",
        "//src/character:progression",
        "//src/frontend:panel_widths",
        "//src/frontend:types",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:inventory_list",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:item_row",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
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
    size = "small",
    srcs = ["inventory_panel_test.cc"],
    deps = [
        ":inventory_panel",
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:screen_text",
        "//src/testing:prototypes",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "menu_panel",
    srcs = ["menu_panel.cc"],
    hdrs = ["menu_panel.h"],
    deps = [
        "//src:build_config",
        "//src:game_state",
        "//src/character:progression",
        "//src/combat:battle_analysis",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "menu_panel_test",
    size = "small",
    srcs = ["menu_panel_test.cc"],
    deps = [
        ":menu_panel",
        "//src:game_state",
        "//src/combat:battle_analysis",
        "//src/frontend:types",
        "//src/frontend/widgets:screen_text",
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
        "//src:game_state",
        "//src/combat:fight",
        "//src/frontend:panel_widths",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/protos:map_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "combat_panel_test",
    size = "small",
    srcs = ["combat_panel_test.cc"],
    deps = [
        ":combat_panel",
        "//src:game_state",
        "//src/combat:encounter",
        "//src/combat:fight",
        "//src/frontend:panel_widths",
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

cc_library(
    name = "hotkeys_panel",
    srcs = ["hotkeys_panel.cc"],
    hdrs = ["hotkeys_panel.h"],
    deps = [
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "level_up_popup_panel",
    srcs = ["level_up_popup_panel.cc"],
    hdrs = ["level_up_popup_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "level_up_popup_panel_test",
    size = "small",
    srcs = ["level_up_popup_panel_test.cc"],
    deps = [
        ":level_up_popup_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "offline_popup_panel",
    srcs = ["offline_popup_panel.cc"],
    hdrs = ["offline_popup_panel.h"],
    deps = [
        "//src/combat",
        "//src/combat:offline",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "offline_popup_panel_test",
    size = "small",
    srcs = ["offline_popup_panel_test.cc"],
    deps = [
        ":offline_popup_panel",
        "//src/combat:offline",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "advancement_popup_panel",
    srcs = ["advancement_popup_panel.cc"],
    hdrs = ["advancement_popup_panel.h"],
    deps = [
        "//src/character:job_name",
        "//src/frontend/widgets:chrome",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "advancement_popup_panel_test",
    size = "small",
    srcs = ["advancement_popup_panel_test.cc"],
    deps = [
        ":advancement_popup_panel",
        ":level_up_popup_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "death_popup_panel",
    srcs = ["death_popup_panel.cc"],
    hdrs = ["death_popup_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "death_popup_panel_test",
    size = "small",
    srcs = ["death_popup_panel_test.cc"],
    deps = [
        ":death_popup_panel",
        ":level_up_popup_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "hotkeys_panel_test",
    size = "small",
    srcs = ["hotkeys_panel_test.cc"],
    deps = [
        ":hotkeys_panel",
        "//src/character:progression",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
