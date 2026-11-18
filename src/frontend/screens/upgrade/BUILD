load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "familiar_cube_panel",
    srcs = ["familiar_cube_panel.cc"],
    hdrs = ["familiar_cube_panel.h"],
    deps = [
        "//src/character/account:familiar",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/prompts:reroll_prompt",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:text_columns",
        "//src/protos:familiar_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "familiar_cube_panel_test",
    size = "small",
    srcs = ["familiar_cube_panel_test.cc"],
    deps = [
        ":familiar_cube_panel",
        "//src/character/account:familiar",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/prompts:reroll_prompt",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "scroll_panel",
    srcs = ["scroll_panel.cc"],
    hdrs = ["scroll_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item/upgrade:spell_trace_cost",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "scroll_panel_test",
    size = "small",
    srcs = ["scroll_panel_test.cc"],
    deps = [
        ":scroll_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "star_force_panel",
    srcs = ["star_force_panel.cc"],
    hdrs = ["star_force_panel.h"],
    deps = [
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:text_columns",
        "//src/item:equip_instance",
        "//src/item/upgrade:star_force_cost",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "star_force_panel_test",
    size = "small",
    srcs = ["star_force_panel_test.cc"],
    deps = [
        ":star_force_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trace_recover_panel",
    srcs = ["trace_recover_panel.cc"],
    hdrs = ["trace_recover_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/text:keys",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "trace_recover_panel_test",
    size = "small",
    srcs = ["trace_recover_panel_test.cc"],
    deps = [
        ":trace_recover_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "symbol_combine_panel",
    srcs = ["symbol_combine_panel.cc"],
    hdrs = ["symbol_combine_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:amount_selector",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "symbol_combine_panel_test",
    size = "small",
    srcs = ["symbol_combine_panel_test.cc"],
    deps = [
        ":symbol_combine_panel",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "flame_panel",
    srcs = ["flame_panel.cc"],
    hdrs = ["flame_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/prompts:reroll_prompt",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:text_columns",
        "//src/item:equip_instance",
        "//src/item/upgrade:flame",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "flame_panel_test",
    size = "small",
    srcs = ["flame_panel_test.cc"],
    deps = [
        ":flame_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/item:equip_instance",
        "//src/item/upgrade:flame",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "soul_panel",
    srcs = ["soul_panel.cc"],
    hdrs = ["soul_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/prompts:reroll_prompt",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:text_columns",
        "//src/item",
        "//src/item:currency",
        "//src/item:equip_instance",
        "//src/item/upgrade:soul",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "soul_panel_test",
    size = "small",
    srcs = ["soul_panel_test.cc"],
    deps = [
        ":soul_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/text:colors",
        "//src/item:currency",
        "//src/item:equip_instance",
        "//src/item/upgrade:soul",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "cube_panel",
    srcs = ["cube_panel.cc"],
    hdrs = ["cube_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/prompts:reroll_prompt",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:text_columns",
        "//src/item:equip_instance",
        "//src/item/upgrade:potential",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "cube_panel_test",
    size = "small",
    srcs = ["cube_panel_test.cc"],
    deps = [
        ":cube_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:text_columns",
        "//src/item:equip_instance",
        "//src/item/upgrade:potential",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
