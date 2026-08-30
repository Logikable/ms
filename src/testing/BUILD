load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "prototypes",
    testonly = True,
    srcs = ["prototypes.cc"],
    hdrs = ["prototypes.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
    ],
)
