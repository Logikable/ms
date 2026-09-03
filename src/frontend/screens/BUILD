load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "analysis_panel",
    srcs = ["analysis_panel.cc"],
    hdrs = ["analysis_panel.h"],
    deps = [
        "//src:game_state",
        "//src/character:progression",
        "//src/combat:battle_analysis",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
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
        "//src/combat:battle_analysis",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "keybinds_panel",
    srcs = ["keybinds_panel.cc"],
    hdrs = ["keybinds_panel.h"],
    deps = [
        "//src/frontend:keybinds",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:keys",
        "//src/protos:keybinds_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_test(
    name = "keybinds_panel_test",
    size = "small",
    srcs = ["keybinds_panel_test.cc"],
    deps = [
        ":keybinds_panel",
        "//src/frontend:keybinds",
        "//src/protos:keybinds_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "options_panel",
    srcs = ["options_panel.cc"],
    hdrs = ["options_panel.h"],
    deps = [
        "//src:account",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:keys",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "options_panel_test",
    size = "small",
    srcs = ["options_panel_test.cc"],
    deps = [
        ":options_panel",
        "//src:account",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "inspect_panel",
    srcs = ["inspect_panel.cc"],
    hdrs = ["inspect_panel.h"],
    deps = [
        "//src/character",
        "//src/character:arcane_force",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:scroll_card",
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
    size = "small",
    srcs = ["inspect_panel_test.cc"],
    deps = [
        ":inspect_panel",
        "//src/character",
        "//src/character:arcane_force",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:screen_text",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:skill_proto_cc",
        "//src/testing:prototypes",
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
        "//src:account",
        "//src/character",
        "//src/character:hyper_stats",
        "//src/character:job_name",
        "//src/character:progression",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:stat_rows",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "all_stats_panel_test",
    size = "small",
    srcs = ["all_stats_panel_test.cc"],
    deps = [
        ":all_stats_panel",
        "//src/character:hyper_stats",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hyper_stat_level_panel",
    srcs = ["hyper_stat_level_panel.cc"],
    hdrs = ["hyper_stat_level_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:confirm_prompt",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "hyper_stat_level_panel_test",
    size = "small",
    srcs = ["hyper_stat_level_panel_test.cc"],
    deps = [
        ":hyper_stat_level_panel",
        "//src/frontend/widgets:confirm_prompt",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "job_inspect_panel",
    srcs = ["job_inspect_panel.cc"],
    hdrs = ["job_inspect_panel.h"],
    deps = [
        "//src/character",
        "//src/character:job_name",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "job_inspect_panel_test",
    size = "small",
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
    name = "hyper_stat_inspect_panel",
    srcs = ["hyper_stat_inspect_panel.cc"],
    hdrs = ["hyper_stat_inspect_panel.h"],
    deps = [
        "//src/character:hyper_stats",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:text_columns",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "hyper_stat_inspect_panel_test",
    size = "small",
    srcs = ["hyper_stat_inspect_panel_test.cc"],
    deps = [
        ":hyper_stat_inspect_panel",
        "//src/character:hyper_stats",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "pot_info_panel",
    srcs = ["pot_info_panel.cc"],
    hdrs = ["pot_info_panel.h"],
    deps = [
        "//src/character:consumables",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:text_columns",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "pot_info_panel_test",
    size = "small",
    srcs = ["pot_info_panel_test.cc"],
    deps = [
        ":pot_info_panel",
        "//src/character:consumables",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:text_columns",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "skill_inspect_panel_test",
    size = "small",
    srcs = ["skill_inspect_panel_test.cc"],
    deps = [
        ":skill_inspect_panel",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/testing:prototypes",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
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
    size = "small",
    srcs = ["scroll_panel_test.cc"],
    deps = [
        ":scroll_panel",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:marquee",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:text_columns",
        "//src/item:equip_instance",
        "//src/item:star_force_cost",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:screen_text",
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
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:keys",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:inventory_list",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
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
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:screen_text",
        "//src/item:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "symbol_combine_panel",
    srcs = ["symbol_combine_panel.cc"],
    hdrs = ["symbol_combine_panel.h"],
    deps = [
        "//src/frontend/widgets:amount_selector",
        "//src/frontend/widgets:chrome",
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
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "cube_panel",
    srcs = ["cube_panel.cc"],
    hdrs = ["cube_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:keys",
        "//src/item:equip_instance",
        "//src/item:potential",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:panel_test_base",
        "//src/item:equip_instance",
        "//src/item:potential",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hammer_panel",
    srcs = ["hammer_panel.cc"],
    hdrs = ["hammer_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "//src/item:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "hammer_panel_test",
    size = "small",
    srcs = ["hammer_panel_test.cc"],
    deps = [
        ":hammer_panel",
        "//src/frontend/widgets:panel_test_base",
        "//src/item:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "symbol_level_panel",
    srcs = ["symbol_level_panel.cc"],
    hdrs = ["symbol_level_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "symbol_level_panel_test",
    size = "small",
    srcs = ["symbol_level_panel_test.cc"],
    deps = [
        ":symbol_level_panel",
        "//src/frontend/widgets:panel_test_base",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:text_columns",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:text_columns",
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
        "//src/combat:boss_run",
        "//src/combat:test_authority",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:marquee",
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
    name = "boss_clear_panel",
    srcs = ["boss_clear_panel.cc"],
    hdrs = ["boss_clear_panel.h"],
    deps = [
        "//src/combat:boss_run",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "boss_clear_panel_test",
    size = "small",
    srcs = ["boss_clear_panel_test.cc"],
    deps = [
        ":boss_clear_panel",
        "//src/combat:boss_run",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
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
        "//src:spawn",
        "//src/character:boss_reset",
        "//src/character:honor",
        "//src/combat:drop",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:keys",
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
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:screen_text",
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
    name = "mob_inspect_panel",
    srcs = ["mob_inspect_panel.cc"],
    hdrs = ["mob_inspect_panel.h"],
    deps = [
        "//src:game_state",
        "//src:spawn",
        "//src/character:arcane_force",
        "//src/character:honor",
        "//src/combat:loot",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:keys",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "mob_inspect_panel_test",
    size = "small",
    srcs = ["mob_inspect_panel_test.cc"],
    deps = [
        ":mob_inspect_panel",
        "//src:game_state",
        "//src/frontend/widgets:panel_test_base",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "party_inspect_panel",
    srcs = ["party_inspect_panel.cc"],
    hdrs = ["party_inspect_panel.h"],
    deps = [
        ":all_stats_panel",
        "//src:game_state",
        "//src/character",
        "//src/character:progression",
        "//src/character:stat_preset",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:equipped_list",
        "//src/frontend/widgets:item_columns",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/item:equip_instance",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@protobuf",
    ],
)

cc_test(
    name = "party_inspect_panel_test",
    size = "small",
    srcs = ["party_inspect_panel_test.cc"],
    deps = [
        ":all_stats_panel",
        ":party_inspect_panel",
        "//src/frontend/widgets:panel_test_base",
        "//src/frontend/widgets:stat_rows",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "party_select_panel",
    srcs = ["party_select_panel.cc"],
    hdrs = ["party_select_panel.h"],
    deps = [
        "//src/character",
        "//src/character:job_name",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/multiplayer:client",
        "//src/multiplayer:protocol",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "party_select_panel_test",
    size = "small",
    srcs = ["party_select_panel_test.cc"],
    deps = [
        ":party_select_panel",
        "//src/frontend:types",
        "//src/frontend/widgets:screen_text",
        "//src/multiplayer:client",
        "//src/protos:multiplayer_proto_cc",
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
        "//src:map_level",
        "//src:spawn",
        "//src/character:arcane_force",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
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
        "//src/frontend/widgets:format",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
