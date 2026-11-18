load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "arcane_force",
    srcs = ["arcane_force.cc"],
    hdrs = ["arcane_force.h"],
)

cc_library(
    name = "symbol",
    srcs = ["symbol.cc"],
    hdrs = ["symbol.h"],
    deps = [
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "symbol_test",
    size = "small",
    srcs = ["symbol_test.cc"],
    deps = [
        ":symbol",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sacred_power",
    srcs = ["sacred_power.cc"],
    hdrs = ["sacred_power.h"],
    deps = [":arcane_force"],
)
