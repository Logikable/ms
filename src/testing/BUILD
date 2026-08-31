load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "prototypes",
    testonly = True,
    srcs = ["prototypes.cc"],
    hdrs = ["prototypes.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:map_proto_cc",
        "//src/protos:mob_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)

cc_library(
    name = "data_files",
    testonly = True,
    srcs = ["data_files.cc"],
    hdrs = ["data_files.h"],
    deps = [
        "//src:proto_loader",
        "@abseil-cpp//absl/log:check",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)
