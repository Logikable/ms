load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "item",
    srcs = ["item.cc"],
    hdrs = ["item.h"],
    deps = [
        ":equip_stats",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_library(
    name = "equip_instance",
    srcs = ["equip_instance.cc"],
    hdrs = ["equip_instance.h"],
    deps = [
        ":equip_stats",
        ":item",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
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
    deps = [":equip_instance"],
)

cc_test(
    name = "star_force_cost_test",
    srcs = ["star_force_cost_test.cc"],
    deps = [
        ":star_force_cost",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "projectile",
    srcs = ["projectile.cc"],
    hdrs = ["projectile.h"],
    deps = ["//src/protos:equip_proto_cc"],
)

cc_test(
    name = "projectile_test",
    srcs = ["projectile_test.cc"],
    deps = [
        ":projectile",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_library(
    name = "equip_stats",
    srcs = ["equip_stats.cc"],
    hdrs = ["equip_stats.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "inventory",
    srcs = ["inventory.cc"],
    hdrs = ["inventory.h"],
    deps = [
        ":equip_instance",
        ":item",
    ],
)

cc_library(
    name = "shop",
    srcs = ["shop.cc"],
    hdrs = ["shop.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_test(
    name = "item_test",
    srcs = ["item_test.cc"],
    deps = [
        ":item",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
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

cc_test(
    name = "spell_trace_cost_test",
    srcs = ["spell_trace_cost_test.cc"],
    deps = [
        ":spell_trace_cost",
        "//src/protos:scroll_proto_cc",
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
    name = "inventory_test",
    srcs = ["inventory_test.cc"],
    deps = [
        ":inventory",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "shop_test",
    srcs = ["shop_test.cc"],
    data = [
        "//data/equip:all",
        "//data/items:all",
    ],
    deps = [
        ":shop",
        "//src:proto_loader",
        "//src/character:exp_table",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)
