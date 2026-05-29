load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
  name = "map_proto",
  srcs = ["map.proto"],
)

cc_proto_library(
  name = "map_proto_cc",
  deps = [":map_proto"],
)

proto_library(
  name = "equip_proto",
  srcs = ["equip.proto"],
)

cc_proto_library(
  name = "equip_proto_cc",
  deps = [":equip_proto"],
)

proto_library(
  name = "scroll_proto",
  srcs = ["scroll.proto"],
  deps = [":equip_proto"],
)

cc_proto_library(
  name = "scroll_proto_cc",
  deps = [":scroll_proto"],
)

cc_library(
  name = "proto_loader",
  hdrs = ["proto_loader.h"],
  srcs = ["proto_loader.cc"],
  deps = [
    "@abseil-cpp//absl/log",
    "@protobuf//:protobuf",
  ],
)

cc_library(
  name = "frontend",
  hdrs = ["frontend.h"],
  srcs = ["frontend.cc"],
  linkopts = ["-lreadline"],
)

cc_binary(
  name = "ms",
  srcs = ["main.cc"],
  data = [
    "//data/equip:all",
    "//data/scrolls:all",
  ],
  deps = [
    ":equip_instance",
    ":equip_proto_cc",
    ":frontend",
    ":proto_loader",
    ":scroll_proto_cc",
    "@bazel_tools//tools/cpp/runfiles",
  ],
)

cc_library(
  name = "equip_stats",
  hdrs = ["equip_stats.h"],
  srcs = ["equip_stats.cc"],
  deps = [":equip_proto_cc"],
)

cc_test(
  name = "equip_instance_test",
  srcs = ["equip_instance_test.cc"],
  deps = [
    ":equip_instance",
    ":equip_proto_cc",
    ":scroll_proto_cc",
    "@googletest//:gtest_main",
  ],
)

cc_library(
  name = "equip_instance",
  hdrs = ["equip_instance.h"],
  srcs = ["equip_instance.cc"],
  deps = [
    ":equip_stats",
    ":equip_proto_cc",
    ":scroll_proto_cc",
  ],
)
