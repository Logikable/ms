load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "analysis_panel",
    srcs = ["analysis_panel.cc"],
    hdrs = ["analysis_panel.h"],
    deps = [
        "//src:game_state",
        "//src/character:progression",
        "//src/combat/boss:battle_analysis",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "analysis_panel_test",
    size = "small",
    srcs = ["analysis_panel_test.cc"],
    deps = [
        ":analysis_panel",
        "//src:game_state",
        "//src/combat/boss:battle_analysis",
        "//src/frontend/testing:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "level_up_panel",
    srcs = ["level_up_panel.cc"],
    hdrs = ["level_up_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "level_up_panel_test",
    size = "small",
    srcs = ["level_up_panel_test.cc"],
    deps = [
        ":level_up_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:format",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "map_select_panel",
    srcs = ["map_select_panel.cc"],
    hdrs = ["map_select_panel.h"],
    deps = [
        "//src:game_state",
        "//src:map_force",
        "//src:map_level",
        "//src:spawn",
        "//src/character:exp_table",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "map_select_panel_test",
    size = "small",
    srcs = ["map_select_panel_test.cc"],
    deps = [
        ":map_select_panel",
        "//src:game_state",
        "//src/frontend:types",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:format",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
