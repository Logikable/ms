load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "character_panel",
    srcs = ["character_panel.cc"],
    hdrs = ["character_panel.h"],
    deps = [
        "//src:character",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "equipped_panel",
    srcs = ["equipped_panel.cc"],
    hdrs = ["equipped_panel.h"],
    deps = [
        "//src:character",
        "//src:equip_instance",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "bag_panel",
    srcs = ["bag_panel.cc"],
    hdrs = ["bag_panel.h"],
    deps = [
        "//src:character",
        "//src:equip_instance",
        "//src/protos:equip_proto_cc",
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
    name = "bag_panel_test",
    srcs = ["bag_panel_test.cc"],
    deps = [
        ":bag_panel",
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
    name = "item_menu",
    srcs = ["item_menu.cc"],
    hdrs = ["item_menu.h"],
    deps = [
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
    name = "scroll_panel",
    srcs = ["scroll_panel.cc"],
    hdrs = ["scroll_panel.h"],
    deps = [
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
        "//src/protos:scroll_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "tui_controller",
    srcs = ["tui_controller.cc"],
    hdrs = ["tui_controller.h"],
    deps = [
        ":bag_panel",
        ":equipped_panel",
        ":item_menu",
        ":scroll_panel",
        "//src:character",
        "//src:game_state",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
    ],
)

cc_library(
    name = "tui",
    srcs = ["tui.cc"],
    hdrs = ["tui.h"],
    deps = [
        ":bag_panel",
        ":character_panel",
        ":equipped_panel",
        ":item_menu",
        ":scroll_panel",
        ":tui_controller",
        "//src:character",
        "//src:game_state",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)
