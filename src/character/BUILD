load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "character",
    srcs = ["character.cc"],
    hdrs = ["character.h"],
    deps = [
        ":exp_table",
        "//src/item:equip_instance",
        "//src/item:equip_stats",
        "//src/item:inventory",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "character_stats",
    srcs = ["character_stats.cc"],
    hdrs = ["character_stats.h"],
    deps = [
        ":character",
        "//src/item:equip_stats",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "progression",
    srcs = ["progression.cc"],
    hdrs = ["progression.h"],
    deps = [
        ":character",
        ":exp_table",
        "//src/protos:character_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_test(
    name = "progression_test",
    srcs = ["progression_test.cc"],
    deps = [
        ":character",
        ":exp_table",
        ":progression",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "exp_table",
    srcs = ["exp_table.cc"],
    hdrs = ["exp_table.h"],
)

cc_library(
    name = "job_advancement",
    srcs = ["job_advancement.cc"],
    hdrs = ["job_advancement.h"],
    deps = [
        ":character",
        "//src:game_state",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
    ],
)

cc_test(
    name = "character_test",
    srcs = ["character_test.cc"],
    deps = [
        ":character",
        ":exp_table",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:scroll_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "character_stats_test",
    srcs = ["character_stats_test.cc"],
    deps = [
        ":character",
        ":character_stats",
        "//src/item:equip_instance",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "exp_table_test",
    srcs = ["exp_table_test.cc"],
    deps = [
        ":exp_table",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "job_advancement_test",
    srcs = ["job_advancement_test.cc"],
    data = ["//data/equip:all"],
    deps = [
        ":job_advancement",
        "//src:proto_loader",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest_main",
    ],
)
