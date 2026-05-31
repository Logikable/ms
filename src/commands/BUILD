load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
  name = "util",
  hdrs = ["util.h"],
  srcs = ["util.cc"],
  deps = [
    "//src:equip_instance",
    "//src:equip_proto_cc",
  ],
)

cc_library(
  name = "equip_command",
  hdrs = ["equip.h"],
  srcs = ["equip.cc"],
  deps = [
    "//src:character",
    "//src:frontend",
  ],
)

cc_library(
  name = "unequip_command",
  hdrs = ["unequip.h"],
  srcs = ["unequip.cc"],
  deps = [
    ":util",
    "//src:character",
    "//src:equip_proto_cc",
    "//src:frontend",
  ],
)

cc_library(
  name = "inv_command",
  hdrs = ["inv.h"],
  srcs = ["inv.cc"],
  deps = [
    ":util",
    "//src:character",
    "//src:equip_instance",
    "//src:equip_proto_cc",
    "//src:frontend",
  ],
)

cc_library(
  name = "scroll_command",
  hdrs = ["scroll.h"],
  srcs = ["scroll.cc"],
  deps = [
    ":util",
    "//src:character",
    "//src:equip_instance",
    "//src:equip_proto_cc",
    "//src:frontend",
    "//src:scroll_proto_cc",
  ],
)

cc_test(
  name = "util_test",
  srcs = ["util_test.cc"],
  deps = [
    ":util",
    "//src:equip_instance",
    "//src:equip_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_test(
  name = "equip_command_test",
  srcs = ["equip_test.cc"],
  deps = [
    ":equip_command",
    "//src:character",
    "//src:character_proto_cc",
    "//src:equip_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_test(
  name = "unequip_command_test",
  srcs = ["unequip_test.cc"],
  deps = [
    ":unequip_command",
    "//src:character",
    "//src:character_proto_cc",
    "//src:equip_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_test(
  name = "inv_command_test",
  srcs = ["inv_test.cc"],
  deps = [
    ":inv_command",
    "//src:character",
    "//src:character_proto_cc",
    "//src:equip_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_test(
  name = "scroll_command_test",
  srcs = ["scroll_test.cc"],
  deps = [
    ":scroll_command",
    "//src:character",
    "//src:character_proto_cc",
    "//src:equip_proto_cc",
    "//src:scroll_proto_cc",
    "@googletest//:gtest_main",
  ],
)
