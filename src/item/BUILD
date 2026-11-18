load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "item",
    srcs = ["item.cc"],
    hdrs = ["item.h"],
    deps = [
        ":equip_stats",
        "//src/item/upgrade:flame",
        "//src/item/upgrade:soul",
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
        "//src/item/upgrade:flame",
        "//src/item/upgrade:potential",
        "//src/item/upgrade:soul",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
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
    size = "small",
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
    name = "slot_order",
    srcs = ["slot_order.cc"],
    hdrs = ["slot_order.h"],
    deps = [
        ":item",
        "//src/protos:equip_proto_cc",
    ],
)

cc_library(
    name = "inventory_sort",
    srcs = ["inventory_sort.cc"],
    hdrs = ["inventory_sort.h"],
    deps = [
        ":item",
        ":slot_order",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "inventory_sort_test",
    size = "small",
    srcs = ["inventory_sort_test.cc"],
    deps = [
        ":equip_instance",
        ":inventory_sort",
        ":slot_order",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "currency",
    srcs = ["currency.cc"],
    hdrs = ["currency.h"],
    deps = [
        ":slot_order",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@protobuf",
    ],
)

cc_library(
    name = "tradeable",
    srcs = ["tradeable.cc"],
    hdrs = ["tradeable.h"],
    deps = [
        ":currency",
        "//src/character/force:symbol",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_test(
    name = "tradeable_test",
    size = "small",
    srcs = ["tradeable_test.cc"],
    deps = [
        ":tradeable",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "currency_test",
    size = "small",
    srcs = ["currency_test.cc"],
    deps = [
        ":currency",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_library(
    name = "inventory",
    srcs = ["inventory.cc"],
    hdrs = ["inventory.h"],
    deps = [
        ":equip_instance",
        ":inventory_sort",
        ":item",
        "//src/protos:equip_proto_cc",
    ],
)

cc_library(
    name = "stack_tab",
    srcs = ["stack_tab.cc"],
    hdrs = ["stack_tab.h"],
    deps = [
        ":inventory_sort",
        ":item",
        "//src/protos:item_proto_cc",
        "@protobuf",
    ],
)

cc_test(
    name = "stack_tab_test",
    size = "small",
    srcs = ["stack_tab_test.cc"],
    deps = [
        ":stack_tab",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_library(
    name = "bank",
    srcs = ["bank.cc"],
    hdrs = ["bank.h"],
    deps = [
        ":currency",
        ":equip_instance",
        ":inventory",
        ":item",
        ":stack_tab",
        "//src/protos:account_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_test(
    name = "bank_test",
    size = "small",
    srcs = ["bank_test.cc"],
    deps = [
        ":bank",
        "//src/protos:account_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ring_box",
    srcs = ["ring_box.cc"],
    hdrs = ["ring_box.h"],
    deps = [
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_test(
    name = "ring_box_test",
    size = "small",
    srcs = ["ring_box_test.cc"],
    deps = [
        ":ring_box",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "shop",
    srcs = ["shop.cc"],
    hdrs = ["shop.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":item",
        ":projectile",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
    ],
)

cc_test(
    name = "item_test",
    size = "small",
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
    size = "small",
    srcs = ["equip_instance_test.cc"],
    deps = [
        ":equip_instance",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "equip_stats_test",
    size = "small",
    srcs = ["equip_stats_test.cc"],
    deps = [
        ":equip_stats",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "inventory_test",
    size = "small",
    srcs = ["inventory_test.cc"],
    deps = [
        ":inventory",
        "//src/protos:equip_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "shop_test",
    size = "small",
    srcs = ["shop_test.cc"],
    data = [
        "//data/equip:all",
        "//data/items:all",
    ],
    deps = [
        ":shop",
        "//src/character:exp_table",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/testing:data_files",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)
