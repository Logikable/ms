load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
  name = "character_panel",
  hdrs = ["character_panel.h"],
  srcs = ["character_panel.cc"],
  deps = [
    "//src:character",
    "//src/protos:character_proto_cc",
    "@ftxui//:dom",
  ],
)

cc_library(
  name = "equipped_panel",
  hdrs = ["equipped_panel.h"],
  srcs = ["equipped_panel.cc"],
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
  hdrs = ["bag_panel.h"],
  srcs = ["bag_panel.cc"],
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
  hdrs = ["item_menu.h"],
  srcs = ["item_menu.cc"],
  deps = [
    "@ftxui//:dom",
  ],
)

cc_library(
  name = "scroll_overlay",
  hdrs = ["scroll_overlay.h"],
)

cc_library(
  name = "tui",
  hdrs = ["tui.h"],
  srcs = ["tui.cc"],
  deps = [
    ":bag_panel",
    ":character_panel",
    ":equipped_panel",
    "//src:character",
    "//src:game_state",
    "@ftxui//:component",
    "@ftxui//:dom",
  ],
)
