load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sell_panel",
    srcs = ["sell_panel.cc"],
    hdrs = ["sell_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:amount_selector",
        "//src/frontend/widgets/text:format",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "sell_panel_test",
    size = "small",
    srcs = ["sell_panel_test.cc"],
    deps = [
        ":sell_panel",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "multi_sell_panel",
    srcs = ["multi_sell_panel.cc"],
    hdrs = ["multi_sell_panel.h"],
    deps = [
        "//src:account",
        "//src/character",
        "//src/character:progression",
        "//src/frontend:placement",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:inventory_list",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "multi_sell_panel_test",
    size = "small",
    srcs = ["multi_sell_panel_test.cc"],
    deps = [
        ":multi_sell_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "//src/item:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sell_equip_panel",
    srcs = ["sell_equip_panel.cc"],
    hdrs = ["sell_equip_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "sell_equip_panel_test",
    size = "small",
    srcs = ["sell_equip_panel_test.cc"],
    deps = [
        ":sell_equip_panel",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "buy_panel",
    srcs = ["buy_panel.cc"],
    hdrs = ["buy_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/prompts:amount_selector",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "buy_panel_test",
    size = "small",
    srcs = ["buy_panel_test.cc"],
    deps = [
        ":buy_panel",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "shop_panel",
    srcs = ["shop_panel.cc"],
    hdrs = ["shop_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:marquee",
        "//src/frontend/widgets/text:text_columns",
        "//src/item",
        "//src/item:shop",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "shop_panel_test",
    size = "small",
    srcs = ["shop_panel_test.cc"],
    deps = [
        ":shop_panel",
        "//src/character",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/item",
        "//src/item:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "bank_panel",
    srcs = ["bank_panel.cc"],
    hdrs = ["bank_panel.h"],
    deps = [
        "//src:account",
        "//src/character",
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:inventory_list",
        "//src/frontend/widgets/lists:item_columns",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "//src/item:bank",
        "//src/item:currency",
        "//src/item:tradeable",
        "//src/protos:item_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "bank_panel_test",
    size = "small",
    srcs = ["bank_panel_test.cc"],
    deps = [
        ":bank_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:item_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "trade_panel",
    srcs = ["trade_panel.cc"],
    hdrs = ["trade_panel.h"],
    deps = [
        "//src:account",
        "//src/character",
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:inventory_list",
        "//src/frontend/widgets/lists:item_columns",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:text_columns",
        "//src/item",
        "//src/item:inventory",
        "//src/item:tradeable",
        "//src/protos:item_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "trade_panel_test",
    size = "small",
    srcs = ["trade_panel_test.cc"],
    deps = [
        ":trade_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/item",
        "//src/protos:item_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "box_panel",
    srcs = ["box_panel.cc"],
    hdrs = ["box_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "//src/frontend/widgets/text:text_columns",
        "//src/item:ring_box",
        "//src/item:shop",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "box_panel_test",
    size = "small",
    srcs = ["box_panel_test.cc"],
    deps = [
        ":box_panel",
        "//src/character",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/prompts:confirm_prompt",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
