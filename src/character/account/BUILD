load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "familiar",
    srcs = ["familiar.cc"],
    hdrs = ["familiar.h"],
    deps = [
        "//src/character:stat_preset",
        "//src/combat:damage",
        "//src/item:potential",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "@abseil-cpp//absl/types:span",
        "@protobuf",
    ],
)

cc_test(
    name = "familiar_test",
    size = "small",
    srcs = ["familiar_test.cc"],
    deps = [
        ":familiar",
        "//src/character:stat_preset",
        "//src/protos:equip_proto_cc",
        "//src/protos:familiar_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "guild",
    srcs = ["guild.cc"],
    hdrs = ["guild.h"],
    deps = [
        "//src/protos:account_proto_cc",
        "//src/protos:boss_proto_cc",
        "@protobuf",
    ],
)

cc_test(
    name = "guild_test",
    size = "small",
    srcs = ["guild_test.cc"],
    deps = [
        ":guild",
        "//src/protos:account_proto_cc",
        "//src/protos:boss_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "link",
    srcs = ["link.cc"],
    hdrs = ["link.h"],
    deps = [
        "//src/character:job_branch",
        "//src/character:stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "link_test",
    size = "small",
    srcs = ["link_test.cc"],
    deps = [
        ":link",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "legion",
    srcs = ["legion.cc"],
    hdrs = ["legion.h"],
    deps = [
        "//src/character:job_branch",
        "//src/character:stat_preset",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
    ],
)

cc_test(
    name = "legion_test",
    size = "small",
    srcs = ["legion_test.cc"],
    deps = [
        ":legion",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "legion_plan",
    srcs = ["legion_plan.cc"],
    hdrs = ["legion_plan.h"],
    deps = [
        ":legion",
        "//src/character",
        "//src/character:stat_preset",
        "//src/protos:legion_proto_cc",
    ],
)

cc_test(
    name = "legion_plan_test",
    size = "small",
    srcs = ["legion_plan_test.cc"],
    deps = [
        ":legion",
        ":legion_plan",
        "//src/character",
        "//src/character:character_stats",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "noblesse_plan",
    srcs = ["noblesse_plan.cc"],
    hdrs = ["noblesse_plan.h"],
    deps = [
        "//src/character",
        "//src/protos:skill_proto_cc",
    ],
)

cc_test(
    name = "noblesse_plan_test",
    size = "small",
    srcs = ["noblesse_plan_test.cc"],
    deps = [
        ":noblesse_plan",
        "//src/character",
        "//src/character:skill_placement",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)
