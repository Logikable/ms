load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "character_panel",
    srcs = ["character_panel.cc"],
    hdrs = ["character_panel.h"],
    deps = [
        "//src/character",
        "//src/character:character_stats",
        "//src/character:progression",
        "//src/combat:damage",
        "//src/frontend:types",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:marquee",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:panel_util",
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
        "//src/frontend/widgets:marquee",
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
        "//src/character:progression",
        "//src/frontend/widgets:colors",
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
        "//src/frontend/widgets:marquee",
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
        "//src/character:progression",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:panel_util",
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

cc_library(
    name = "hotkeys_panel",
    srcs = ["hotkeys_panel.cc"],
    hdrs = ["hotkeys_panel.h"],
    deps = [
        "//src/character:progression",
        "//src/frontend/widgets:panel_util",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "level_up_popup_panel",
    srcs = ["level_up_popup_panel.cc"],
    hdrs = ["level_up_popup_panel.h"],
    deps = [
        "//src/frontend/widgets:panel_util",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "level_up_popup_panel_test",
    srcs = ["level_up_popup_panel_test.cc"],
    deps = [
        ":level_up_popup_panel",
        "//src/frontend/widgets:panel_util",
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
        "//src/frontend/widgets:panel_util",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "advancement_popup_panel_test",
    srcs = ["advancement_popup_panel_test.cc"],
    deps = [
        ":advancement_popup_panel",
        ":level_up_popup_panel",
        "//src/frontend/widgets:panel_util",
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
        "//src/frontend/widgets:panel_util",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "death_popup_panel_test",
    srcs = ["death_popup_panel_test.cc"],
    deps = [
        ":death_popup_panel",
        ":level_up_popup_panel",
        "//src/frontend/widgets:panel_util",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "hotkeys_panel_test",
    srcs = ["hotkeys_panel_test.cc"],
    deps = [
        ":hotkeys_panel",
        "//src/character:progression",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
