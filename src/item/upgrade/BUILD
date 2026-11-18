load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "flame",
    srcs = ["flame.cc"],
    hdrs = ["flame.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_test(
    name = "flame_test",
    size = "small",
    srcs = ["flame_test.cc"],
    deps = [
        ":flame",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "soul",
    srcs = ["soul.cc"],
    hdrs = ["soul.h"],
    deps = [
        ":potential",
        "//src/protos:boss_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_test(
    name = "soul_test",
    size = "small",
    srcs = ["soul_test.cc"],
    deps = [
        ":soul",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "potential",
    srcs = ["potential.cc"],
    hdrs = ["potential.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "potential_test",
    size = "small",
    srcs = ["potential_test.cc"],
    deps = [
        ":potential",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "spell_trace_cost",
    srcs = ["spell_trace_cost.cc"],
    hdrs = ["spell_trace_cost.h"],
    deps = [
        "//src/protos:scroll_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "star_force_cost",
    srcs = ["star_force_cost.cc"],
    hdrs = ["star_force_cost.h"],
    deps = ["//src/item:equip_instance"],
)

cc_test(
    name = "spell_trace_cost_test",
    size = "small",
    srcs = ["spell_trace_cost_test.cc"],
    deps = [
        ":spell_trace_cost",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)
