load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "types",
    hdrs = ["types.h"],
    deps = [
        "//src/item:equip_instance",
    ],
)

cc_library(
    name = "item_ref",
    srcs = ["item_ref.cc"],
    hdrs = ["item_ref.h"],
    deps = [
        "//src/character",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
    ],
)

cc_test(
    name = "item_ref_test",
    srcs = ["item_ref_test.cc"],
    deps = [
        ":item_ref",
        "//src/frontend/widgets:panel_test_base",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tui_controller",
    srcs = ["tui_controller.cc"],
    hdrs = ["tui_controller.h"],
    deps = [
        ":item_ref",
        ":types",
        "//src:game_state",
        "//src/character",
        "//src/character:job_advancement",
        "//src/frontend/panels:character_panel",
        "//src/frontend/panels:equipped_panel",
        "//src/frontend/panels:inventory_panel",
        "//src/frontend/screens:buy_panel",
        "//src/frontend/screens:job_inspect_panel",
        "//src/frontend/screens:map_select_panel",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/screens:sell_equip_panel",
        "//src/frontend/screens:sell_panel",
        "//src/frontend/screens:shop_panel",
        "//src/frontend/screens:star_force_panel",
        "//src/frontend/screens:trace_recover_panel",
        "//src/frontend/widgets:amount_selector",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:panel_util",
        "//src/item",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
    ],
)

cc_test(
    name = "tui_controller_test",
    srcs = ["tui_controller_test.cc"],
    deps = [
        ":tui_controller",
        "//src:game_state",
        "//src/character:job_advancement",
        "//src/character:progression",
        "//src/frontend/panels:character_panel",
        "//src/frontend/panels:equipped_panel",
        "//src/frontend/panels:inventory_panel",
        "//src/frontend/screens:buy_panel",
        "//src/frontend/screens:job_inspect_panel",
        "//src/frontend/screens:map_select_panel",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/screens:sell_equip_panel",
        "//src/frontend/screens:sell_panel",
        "//src/frontend/screens:shop_panel",
        "//src/frontend/screens:star_force_panel",
        "//src/frontend/screens:trace_recover_panel",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:panel_util",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "main_layout",
    srcs = ["main_layout.cc"],
    hdrs = ["main_layout.h"],
    deps = [
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "main_layout_test",
    srcs = ["main_layout_test.cc"],
    deps = [
        ":main_layout",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "celebration",
    srcs = ["celebration.cc"],
    hdrs = ["celebration.h"],
    deps = [
        ":types",
        "//src/character:progression",
        "//src/frontend/panels:advancement_popup_panel",
        "//src/frontend/panels:death_popup_panel",
        "//src/frontend/panels:level_up_popup_panel",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "celebration_test",
    srcs = ["celebration_test.cc"],
    deps = [
        ":celebration",
        ":types",
        "//src/character:progression",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tui",
    srcs = ["tui.cc"],
    hdrs = ["tui.h"],
    deps = [
        ":celebration",
        ":main_layout",
        ":tui_controller",
        "//src:game_state",
        "//src:save",
        "//src/character",
        "//src/character:exp_table",
        "//src/character:job_advancement",
        "//src/character:progression",
        "//src/combat",
        "//src/combat:fight",
        "//src/frontend/panels:character_panel",
        "//src/frontend/panels:combat_panel",
        "//src/frontend/panels:equipped_panel",
        "//src/frontend/panels:hotkeys_panel",
        "//src/frontend/panels:inventory_panel",
        "//src/frontend/screens:all_stats_panel",
        "//src/frontend/screens:buy_panel",
        "//src/frontend/screens:inspect_panel",
        "//src/frontend/screens:job_inspect_panel",
        "//src/frontend/screens:map_select_panel",
        "//src/frontend/screens:scroll_panel",
        "//src/frontend/screens:sell_equip_panel",
        "//src/frontend/screens:sell_panel",
        "//src/frontend/screens:shop_panel",
        "//src/frontend/screens:skill_inspect_panel",
        "//src/frontend/screens:star_force_panel",
        "//src/frontend/screens:trace_recover_panel",
        "//src/frontend/widgets:amount_selector",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:marquee",
        "//src/frontend/widgets:panel_util",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
