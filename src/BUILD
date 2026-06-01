load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_test(
  name = "character_test",
  srcs = ["character_test.cc"],
  deps = [
    ":character",
    "//src/protos:character_proto_cc",
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_library(
  name = "character",
  hdrs = ["character.h"],
  srcs = ["character.cc"],
  deps = [
    "//src/protos:character_proto_cc",
    ":equip_instance",
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
    "@abseil-cpp//absl/log",
  ],
)

cc_library(
  name = "proto_loader",
  hdrs = ["proto_loader.h"],
  srcs = ["proto_loader.cc"],
  deps = [
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
    "@abseil-cpp//absl/log",
    "@protobuf//:protobuf",
  ],
)

cc_library(
  name = "game_state",
  hdrs = ["game_state.h"],
  srcs = ["game_state.cc"],
  deps = [
    ":character",
    "//src/protos:character_proto_cc",
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
  ],
)

cc_binary(
  name = "ms",
  srcs = ["main.cc"],
  data = [
    "//data/equip:all",
    "//data/scrolls:all",
  ],
  deps = [
    ":game_state",
    ":proto_loader",
    "//src/frontend:tui",
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
    "@bazel_tools//tools/cpp/runfiles",
  ],
)

cc_library(
  name = "equip_stats",
  hdrs = ["equip_stats.h"],
  srcs = ["equip_stats.cc"],
  deps = [
    "//src/protos:equip_proto_cc",
    "@abseil-cpp//absl/types:span",
  ],
)

cc_test(
  name = "proto_loader_test",
  srcs = ["proto_loader_test.cc"],
  deps = [
    "//src/protos:equip_proto_cc",
    ":proto_loader",
    "@googletest//:gtest_main",
  ],
)

cc_test(
  name = "equip_stats_test",
  srcs = ["equip_stats_test.cc"],
  deps = [
    ":equip_stats",
    "//src/protos:equip_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_test(
  name = "equip_instance_test",
  srcs = ["equip_instance_test.cc"],
  deps = [
    ":equip_instance",
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
    "@googletest//:gtest_main",
  ],
)


cc_library(
  name = "equip_instance",
  hdrs = ["equip_instance.h"],
  srcs = ["equip_instance.cc"],
  deps = [
    ":equip_stats",
    "//src/protos:equip_proto_cc",
    "//src/protos:scroll_proto_cc",
  ],
)
