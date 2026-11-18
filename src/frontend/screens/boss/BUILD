load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "boss_fight_panel",
    srcs = ["boss_fight_panel.cc"],
    hdrs = ["boss_fight_panel.h"],
    deps = [
        "//src/combat/boss:boss_run",
        "//src/frontend:placement",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:marquee",
        "//src/frontend/widgets/text:text_columns",
        "//src/protos:boss_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_fight_panel_test",
    size = "small",
    srcs = ["boss_fight_panel_test.cc"],
    deps = [
        ":boss_fight_panel",
        "//src:game_state",
        "//src/combat:test_authority",
        "//src/combat/boss:boss_run",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:marquee",
        "//src/item:equip_instance",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_analysis_panel",
    srcs = ["boss_analysis_panel.cc"],
    hdrs = ["boss_analysis_panel.h"],
    deps = [
        "//src/combat:damage_breakdown",
        "//src/frontend:placement",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_analysis_panel_test",
    size = "small",
    srcs = ["boss_analysis_panel_test.cc"],
    deps = [
        ":boss_analysis_panel",
        "//src/combat:damage_breakdown",
        "//src/frontend:placement",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_clear_panel",
    srcs = ["boss_clear_panel.cc"],
    hdrs = ["boss_clear_panel.h"],
    deps = [
        "//src/combat/boss:boss_run",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_clear_panel_test",
    size = "small",
    srcs = ["boss_clear_panel_test.cc"],
    deps = [
        ":boss_clear_panel",
        "//src/combat/boss:boss_run",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_select_panel",
    srcs = ["boss_select_panel.cc"],
    hdrs = ["boss_select_panel.h"],
    deps = [
        "//src:game_state",
        "//src:map_force",
        "//src:spawn",
        "//src/character:boss_reset",
        "//src/character:honor",
        "//src/combat:drop",
        "//src/combat:loot",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:marquee",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_select_panel_test",
    size = "small",
    srcs = ["boss_select_panel_test.cc"],
    deps = [
        ":boss_select_panel",
        "//src:game_state",
        "//src/character:inner_ability",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
