load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "colors",
    hdrs = ["colors.h"],
    deps = [
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "text_columns",
    srcs = ["text_columns.cc"],
    hdrs = ["text_columns.h"],
    deps = ["@ftxui//:screen"],
)

cc_test(
    name = "text_columns_test",
    size = "small",
    srcs = ["text_columns_test.cc"],
    deps = [
        ":text_columns",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "marquee",
    srcs = ["marquee.cc"],
    hdrs = ["marquee.h"],
    deps = [":text_columns"],
)

cc_test(
    name = "marquee_test",
    size = "small",
    srcs = ["marquee_test.cc"],
    deps = [
        ":marquee",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "keys",
    srcs = ["keys.cc"],
    hdrs = ["keys.h"],
    deps = [
        "@ftxui//:component",
    ],
)

cc_test(
    name = "keys_test",
    size = "small",
    srcs = ["keys_test.cc"],
    deps = [
        ":keys",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "panel_util",
    srcs = ["panel_util.cc"],
    hdrs = ["panel_util.h"],
    deps = [
        ":colors",
        ":marquee",
        ":text_columns",
        "//src/character:hyper_stats",
        "//src/character:inner_ability",
        "//src/item",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_library(
    name = "scroll_card",
    srcs = ["scroll_card.cc"],
    hdrs = ["scroll_card.h"],
    deps = [
        ":panel_util",
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
        ":colors",
        ":marquee",
        ":panel_util",
        "//src/character",
        "//src/item",
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
        ":colors",
        ":inventory_list",
        ":panel_test_base",
        "//src/frontend:panel_widths",
        "//src/item:equip_instance",
        "@ftxui//:dom",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equipped_list",
    srcs = ["equipped_list.cc"],
    hdrs = ["equipped_list.h"],
    deps = [
        ":marquee",
        ":panel_util",
        "//src/character",
        "//src/character:arcane_force",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "equipped_list_test",
    size = "small",
    srcs = ["equipped_list_test.cc"],
    deps = [
        ":equipped_list",
        ":panel_test_base",
        ":text_columns",
        "//src/frontend:panel_widths",
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
        ":panel_util",
        "//src:account",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:hyper_stats",
        "//src/character:progression",
        "//src/combat:constants",
        "//src/combat:damage",
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
        "//src/testing:prototypes",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "panel_util_test",
    size = "small",
    srcs = ["panel_util_test.cc"],
    deps = [
        ":colors",
        ":panel_util",
        "//src/frontend/widgets:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "confirm_prompt",
    srcs = ["confirm_prompt.cc"],
    hdrs = ["confirm_prompt.h"],
    deps = [
        ":panel_util",
        "//src/frontend/widgets:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "text_field",
    srcs = ["text_field.cc"],
    hdrs = ["text_field.h"],
    deps = [
        ":panel_util",
        "//src/frontend/widgets:keys",
        "@ftxui//:component",
    ],
)

cc_test(
    name = "text_field_test",
    size = "small",
    srcs = ["text_field_test.cc"],
    deps = [
        ":text_field",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "confirm_prompt_test",
    size = "small",
    srcs = ["confirm_prompt_test.cc"],
    deps = [
        ":confirm_prompt",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "continue_prompt",
    srcs = ["continue_prompt.cc"],
    hdrs = ["continue_prompt.h"],
    deps = [
        ":panel_util",
        "//src/frontend/widgets:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "continue_prompt_test",
    size = "small",
    srcs = ["continue_prompt_test.cc"],
    deps = [
        ":continue_prompt",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "amount_selector",
    srcs = ["amount_selector.cc"],
    hdrs = ["amount_selector.h"],
    deps = [
        ":confirm_prompt",
        ":panel_util",
        "//src/frontend/widgets:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "amount_selector_test",
    size = "small",
    srcs = ["amount_selector_test.cc"],
    deps = [
        ":amount_selector",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "item_menu",
    srcs = ["item_menu.cc"],
    hdrs = ["item_menu.h"],
    deps = [
        ":panel_util",
        "//src/frontend/widgets:keys",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "item_menu_test",
    size = "small",
    srcs = ["item_menu_test.cc"],
    deps = [
        ":colors",
        ":item_menu",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "panel_test_base",
    testonly = True,
    hdrs = ["panel_test_base.h"],
    deps = [
        "//src:account",
        "//src/character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest",
    ],
)
