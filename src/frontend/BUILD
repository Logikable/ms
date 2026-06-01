load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
  name = "util",
  hdrs = ["util.h"],
  srcs = ["util.cc"],
  deps = ["//src/protos:equip_proto_cc"],
)

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
    ":util",
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
    ":util",
    "//src:character",
    "//src:equip_instance",
    "//src/protos:equip_proto_cc",
    "@ftxui//:component",
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
    "//src/protos:scroll_proto_cc",
    "@ftxui//:component",
    "@ftxui//:dom",
  ],
)
