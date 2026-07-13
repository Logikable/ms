load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "types",
    hdrs = ["types.h"],
    deps = [
        "//src:equip_instance",
    ],
)

cc_library(
    name = "colors",
    hdrs = ["colors.h"],
    deps = [
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "panel_util",
    srcs = ["panel_util.cc"],
    hdrs = ["panel_util.h"],
    deps = [
        ":colors",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_test(
    name = "panel_util_test",
    srcs = ["panel_util_test.cc"],
    deps = [
        ":colors",
        ":panel_util",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "character_panel",
    srcs = ["character_panel.cc"],
    hdrs = ["character_panel.h"],
    deps = [
        ":colors",
        ":panel_util",
        ":types",
        "//src:character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "combat_panel",
    srcs = ["combat_panel.cc"],
    hdrs = ["combat_panel.h"],
    deps = [
        ":character_panel",
        ":colors",
        ":panel_util",
        "//src:game_state",
        "//src/combat:fight",
        "//src/protos:map_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "combat_panel_test",
    srcs = ["combat_panel_test.cc"],
    deps = [
        ":combat_panel",
        "//src:equip_instance",
        "//src:game_state",
        "//src/combat:encounter",
        "//src/combat:fight",
        "//src/protos:equip_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
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
        ":panel_util",
        "//src:game_state",
        "//src/protos:item_proto_cc",
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
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "equipped_panel",
    srcs = ["equipped_panel.cc"],
    hdrs = ["equipped_panel.h"],
    deps = [
        ":item_menu",
        ":panel_util",
        ":scroll_panel",
        ":types",
        "//src:character",
        "//src:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "inventory_panel",
    srcs = ["inventory_panel.cc"],
    hdrs = ["inventory_panel.h"],
    deps = [
        ":colors",
        ":item_menu",
        ":panel_util",
        ":scroll_panel",
        ":types",
        "//src:character",
        "//src:equip_instance",
        "//src:item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "panel_test_base",
    testonly = True,
    hdrs = ["panel_test_base.h"],
    deps = [
        "//src:character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "character_panel_test",
    srcs = ["character_panel_test.cc"],
    deps = [
        ":character_panel",
        ":panel_test_base",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "inventory_panel_test",
    srcs = ["inventory_panel_test.cc"],
    deps = [
        ":inventory_panel",
        ":panel_test_base",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "equipped_panel_test",
    srcs = ["equipped_panel_test.cc"],
    deps = [
        ":equipped_panel",
        ":panel_test_base",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ap_alloc_panel",
    srcs = ["ap_alloc_panel.cc"],
    hdrs = ["ap_alloc_panel.h"],
    deps = [
        ":panel_util",
        ":types",
        "//src:character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "ap_alloc_panel_test",
    srcs = ["ap_alloc_panel_test.cc"],
    deps = [
        ":ap_alloc_panel",
        ":panel_test_base",
        ":types",
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
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "item_menu_test",
    srcs = ["item_menu_test.cc"],
    deps = [
        ":item_menu",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "inspect_panel",
    srcs = ["inspect_panel.cc"],
    hdrs = ["inspect_panel.h"],
    deps = [
        ":panel_util",
        "//src:item",
        "//src/protos:equip_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "inspect_panel_test",
    srcs = ["inspect_panel_test.cc"],
    deps = [
        ":inspect_panel",
        ":panel_test_base",
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
        ":panel_util",
        ":types",
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
        "//src/protos:equip_proto_cc",
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
        ":panel_util",
        ":types",
        "//src:equip_instance",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "star_force_panel_test",
    srcs = ["star_force_panel_test.cc"],
    deps = [
        ":star_force_panel",
        "//src:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sell_panel",
    srcs = ["sell_panel.cc"],
    hdrs = ["sell_panel.h"],
    deps = [
        ":panel_util",
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
    name = "trace_recover_panel",
    srcs = ["trace_recover_panel.cc"],
    hdrs = ["trace_recover_panel.h"],
    deps = [
        ":panel_util",
        ":types",
        "//src:character",
        "//src:equip_instance",
        "//src:item",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "tui_controller",
    srcs = ["tui_controller.cc"],
    hdrs = ["tui_controller.h"],
    deps = [
        ":ap_alloc_panel",
        ":equipped_panel",
        ":inventory_panel",
        ":panel_util",
        ":scroll_panel",
        ":sell_panel",
        ":star_force_panel",
        ":trace_recover_panel",
        ":types",
        "//src:character",
        "//src:equip_instance",
        "//src:game_state",
        "//src:item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@ftxui//:component",
    ],
)

cc_test(
    name = "tui_controller_test",
    srcs = ["tui_controller_test.cc"],
    deps = [
        ":ap_alloc_panel",
        ":equipped_panel",
        ":inventory_panel",
        ":scroll_panel",
        ":sell_panel",
        ":star_force_panel",
        ":trace_recover_panel",
        ":tui_controller",
        "//src:game_state",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@ftxui//:component",
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
        ":ap_alloc_panel",
        ":character_panel",
        ":colors",
        ":combat_panel",
        ":equipped_panel",
        ":inspect_panel",
        ":inventory_panel",
        ":item_menu",
        ":panel_util",
        ":scroll_panel",
        ":sell_panel",
        ":star_force_panel",
        ":trace_recover_panel",
        ":tui_controller",
        "//src:character",
        "//src:equip_instance",
        "//src:exp_table",
        "//src:game_state",
        "//src/combat",
        "//src/combat:fight",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
