load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "inspect_panel",
    srcs = ["inspect_panel.cc"],
    hdrs = ["inspect_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "inspect_panel_test",
    srcs = ["inspect_panel_test.cc"],
    deps = [
        ":inspect_panel",
        "//src/character",
        "//src/frontend/widgets:panel_test_base",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "all_stats_panel",
    srcs = ["all_stats_panel.cc"],
    hdrs = ["all_stats_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend/widgets:panel_util",
        "//src/frontend/widgets:stat_rows",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "all_stats_panel_test",
    srcs = ["all_stats_panel_test.cc"],
    deps = [
        ":all_stats_panel",
        "//src/frontend/widgets:panel_test_base",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "job_inspect_panel",
    srcs = ["job_inspect_panel.cc"],
    hdrs = ["job_inspect_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:panel_util",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "job_inspect_panel_test",
    srcs = ["job_inspect_panel_test.cc"],
    deps = [
        ":job_inspect_panel",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "skill_inspect_panel",
    srcs = ["skill_inspect_panel.cc"],
    hdrs = ["skill_inspect_panel.h"],
    deps = [
        "//src/character:character_stats",
        "//src/combat:damage",
        "//src/frontend/widgets:panel_util",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "skill_inspect_panel_test",
    srcs = ["skill_inspect_panel_test.cc"],
    deps = [
        ":skill_inspect_panel",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:dom",
        "@ftxui//:screen",
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
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/item:equip_instance",
        "//src/item:spell_trace_cost",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "scroll_panel_test",
    srcs = ["scroll_panel_test.cc"],
    deps = [
        ":scroll_panel",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
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
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:panel_util",
        "//src/item:equip_instance",
        "//src/item:star_force_cost",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "star_force_panel_test",
    srcs = ["star_force_panel_test.cc"],
    deps = [
        ":star_force_panel",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
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
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "trace_recover_panel_test",
    srcs = ["trace_recover_panel_test.cc"],
    deps = [
        ":trace_recover_panel",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:component",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sell_panel",
    srcs = ["sell_panel.cc"],
    hdrs = ["sell_panel.h"],
    deps = [
        "//src/frontend/widgets:amount_selector",
        "//src/frontend/widgets:panel_util",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "sell_panel_test",
    srcs = ["sell_panel_test.cc"],
    deps = [
        ":sell_panel",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:panel_util",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "sell_equip_panel_test",
    srcs = ["sell_equip_panel_test.cc"],
    deps = [
        ":sell_equip_panel",
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
        "//src/frontend/widgets:amount_selector",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_util",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "buy_panel_test",
    srcs = ["buy_panel_test.cc"],
    deps = [
        ":buy_panel",
        "//src/frontend/widgets:colors",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/item:shop",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "shop_panel_test",
    srcs = ["shop_panel_test.cc"],
    deps = [
        ":shop_panel",
        "//src/character",
        "//src/frontend/widgets:colors",
        "//src/item",
        "//src/item:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "boss_fight_panel",
    srcs = ["boss_fight_panel.cc"],
    hdrs = ["boss_fight_panel.h"],
    deps = [
        "//src/combat:boss_run",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_util",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_fight_panel_test",
    srcs = ["boss_fight_panel_test.cc"],
    deps = [
        ":boss_fight_panel",
        "//src:game_state",
        "//src/combat:boss_run",
        "//src/item:equip_instance",
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

cc_library(
    name = "boss_select_panel",
    srcs = ["boss_select_panel.cc"],
    hdrs = ["boss_select_panel.h"],
    deps = [
        "//src:game_state",
        "//src/character:boss_reset",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_util",
        "//src/protos:boss_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_select_panel_test",
    srcs = ["boss_select_panel_test.cc"],
    deps = [
        ":boss_select_panel",
        "//src:game_state",
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

cc_library(
    name = "map_select_panel",
    srcs = ["map_select_panel.cc"],
    hdrs = ["map_select_panel.h"],
    deps = [
        "//src:game_state",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_util",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "map_select_panel_test",
    srcs = ["map_select_panel_test.cc"],
    deps = [
        ":map_select_panel",
        "//src:game_state",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
