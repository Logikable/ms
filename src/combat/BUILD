load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "constants",
    hdrs = ["constants.h"],
)

cc_library(
    name = "damage",
    srcs = ["damage.cc"],
    hdrs = ["damage.h"],
    deps = [
        ":constants",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "damage_test",
    srcs = ["damage_test.cc"],
    deps = [
        ":damage",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "loot",
    srcs = ["loot.cc"],
    hdrs = ["loot.h"],
    deps = ["//src/protos:mob_proto_cc"],
)

cc_test(
    name = "loot_test",
    srcs = ["loot_test.cc"],
    deps = [
        ":loot",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "idle_rate",
    srcs = ["idle_rate.cc"],
    hdrs = ["idle_rate.h"],
    deps = [
        ":constants",
        ":damage",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
    ],
)

cc_test(
    name = "idle_rate_test",
    srcs = ["idle_rate_test.cc"],
    deps = [
        ":constants",
        ":damage",
        ":idle_rate",
        "//src/protos:equip_proto_cc",
        "//src/protos:mob_proto_cc",
        "@googletest//:gtest_main",
    ],
)
