load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "inspect_panel",
    srcs = ["inspect_panel.cc"],
    hdrs = ["inspect_panel.h"],
    deps = [
        "//src/character",
        "//src/character:stat_preset",
        "//src/character/force:symbol",
        "//src/combat:damage",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:scroll_card",
        "//src/frontend/widgets:text_columns",
        "//src/item",
        "//src/item:flame",
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
        "//src/character/force:symbol",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets:colors",
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
        "//src/character:job_name",
        "//src/character:progression",
        "//src/character/matrix:hyper_stats",
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
        "//src/character/matrix:hyper_stats",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
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
        "//src/character/matrix:hyper_stats",
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
        "//src/frontend/testing:panel_test_base",
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
        "//src/character/matrix:hyper_stats",
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
        "//src/character/matrix:hyper_stats",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets:game_names",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "stat_inspect_panel",
    srcs = ["stat_inspect_panel.cc"],
    hdrs = ["stat_inspect_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:stat_rows",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "stat_inspect_panel_test",
    size = "small",
    srcs = ["stat_inspect_panel_test.cc"],
    deps = [
        ":stat_inspect_panel",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets:stat_rows",
        "@ftxui//:dom",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "buff_info_panel",
    srcs = ["buff_info_panel.cc"],
    hdrs = ["buff_info_panel.h"],
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
    name = "buff_info_panel_test",
    size = "small",
    srcs = ["buff_info_panel_test.cc"],
    deps = [
        ":buff_info_panel",
        "//src/character:consumables",
        "//src/frontend/testing:panel_test_base",
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
        "//src/character/account:familiar",
        "//src/character/matrix:v_matrix",
        "//src/combat:damage",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:scroll_card",
        "//src/frontend/widgets:text_columns",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "skill_inspect_screen",
    srcs = ["skill_inspect_screen.cc"],
    hdrs = ["skill_inspect_screen.h"],
    deps = [
        ":skill_inspect_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:keys",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "skill_inspect_screen_test",
    size = "small",
    srcs = ["skill_inspect_screen_test.cc"],
    deps = [
        ":skill_inspect_panel",
        ":skill_inspect_screen",
        "//src/frontend/testing:panel_test_base",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "skill_inspect_panel_test",
    size = "small",
    srcs = ["skill_inspect_panel_test.cc"],
    data = ["//data/skills:all"],
    deps = [
        ":skill_inspect_panel",
        "//src/character/matrix:v_matrix",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets:colors",
        "//src/testing:data_files",
        "//src/testing:prototypes",
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
        "//src:map_force",
        "//src:spawn",
        "//src/character:honor",
        "//src/character/force:arcane_force",
        "//src/character/matrix:v_matrix",
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
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
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
    name = "player_inspect_panel",
    srcs = ["player_inspect_panel.cc"],
    hdrs = ["player_inspect_panel.h"],
    deps = [
        ":all_stats_panel",
        "//src:game_state",
        "//src/character",
        "//src/character/account:link",
        "//src/character/matrix:hyper_stats",
        "//src/frontend:main_layout",
        "//src/frontend:types",
        "//src/frontend/panels:character_panel",
        "//src/frontend/panels:equipped_panel",
        "//src/frontend/widgets:exp_bar",
        "//src/frontend/widgets:keys",
        "//src/item:equip_instance",
        "//src/protos:familiar_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@protobuf",
    ],
)

cc_test(
    name = "player_inspect_panel_test",
    size = "small",
    srcs = ["player_inspect_panel_test.cc"],
    deps = [
        ":player_inspect_panel",
        "//src/character:character_stats",
        "//src/character:skill_placement",
        "//src/frontend:main_layout",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@googletest//:gtest_main",
    ],
)
