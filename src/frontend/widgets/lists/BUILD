load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "item_columns",
    srcs = ["item_columns.cc"],
    hdrs = ["item_columns.h"],
    deps = ["//src/frontend/widgets/text:format"],
)

cc_test(
    name = "item_columns_test",
    size = "small",
    srcs = ["item_columns_test.cc"],
    deps = [
        ":item_columns",
        "//src/frontend/widgets/text:text_columns",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "item_row",
    srcs = ["item_row.cc"],
    hdrs = ["item_row.h"],
    deps = [
        ":item_columns",
        "//src:account",
        "//src/character",
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "//src/item:flame",
        "//src/item:potential",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "item_row_test",
    size = "small",
    srcs = ["item_row_test.cc"],
    deps = [
        ":item_columns",
        ":item_row",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:marquee",
        "//src/frontend/widgets/text:text_columns",
        "//src/protos:equip_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "scroll_card",
    srcs = ["scroll_card.cc"],
    hdrs = ["scroll_card.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "scroll_card_test",
    size = "small",
    srcs = ["scroll_card_test.cc"],
    deps = [
        ":scroll_card",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "inventory_list",
    srcs = ["inventory_list.cc"],
    hdrs = ["inventory_list.h"],
    deps = [
        ":item_columns",
        ":item_row",
        "//src:account",
        "//src/character",
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "//src/item:currency",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "inventory_list_test",
    size = "small",
    srcs = ["inventory_list_test.cc"],
    deps = [
        ":inventory_list",
        ":item_row",
        "//src/frontend:panel_widths",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/text:colors",
        "//src/item:equip_instance",
        "@ftxui//:dom",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "familiar_list",
    srcs = ["familiar_list.cc"],
    hdrs = ["familiar_list.h"],
    deps = [
        ":item_row",
        "//src/character",
        "//src/character/account:familiar",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:text_columns",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "familiar_list_test",
    size = "small",
    srcs = ["familiar_list_test.cc"],
    deps = [
        ":familiar_list",
        "//src/character/account:familiar",
        "//src/frontend/widgets/text:text_columns",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equipped_list",
    srcs = ["equipped_list.cc"],
    hdrs = ["equipped_list.h"],
    deps = [
        ":item_row",
        "//src:account",
        "//src/character",
        "//src/character/force:symbol",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:marquee",
        "//src/item:equip_instance",
        "//src/item:slot_order",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "equipped_list_test",
    size = "small",
    srcs = ["equipped_list_test.cc"],
    deps = [
        ":equipped_list",
        ":item_row",
        "//src/frontend:panel_widths",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets/text:text_columns",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "stat_rows",
    srcs = ["stat_rows.cc"],
    hdrs = ["stat_rows.h"],
    deps = [
        "//src:account",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:job_branch",
        "//src/character:progression",
        "//src/character:stat_preset",
        "//src/character/force:sacred_power",
        "//src/character/matrix:hyper_stats",
        "//src/combat:constants",
        "//src/combat:damage",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "stat_rows_test",
    size = "small",
    srcs = ["stat_rows_test.cc"],
    deps = [
        ":stat_rows",
        "//src/character:character_stats",
        "//src/character:consumables",
        "//src/character/force:sacred_power",
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "item_menu",
    srcs = ["item_menu.cc"],
    hdrs = ["item_menu.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "item_menu_test",
    size = "small",
    srcs = ["item_menu_test.cc"],
    deps = [
        ":item_menu",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
