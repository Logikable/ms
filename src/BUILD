load("@rules_cc//cc:defs.bzl", "cc_library")
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
