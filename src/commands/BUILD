load("@rules_cc//cc:defs.bzl", "cc_library")

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
