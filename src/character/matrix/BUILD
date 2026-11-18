load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "v_matrix",
    srcs = ["v_matrix.cc"],
    hdrs = ["v_matrix.h"],
    deps = ["//src/protos:skill_proto_cc"],
)

cc_library(
    name = "hexa_matrix",
    srcs = ["hexa_matrix.cc"],
    hdrs = ["hexa_matrix.h"],
    deps = [
        ":sol_erda",
        "//src/protos:skill_proto_cc",
    ],
)

cc_library(
    name = "sol_erda",
    srcs = ["sol_erda.cc"],
    hdrs = ["sol_erda.h"],
)

cc_test(
    name = "sol_erda_test",
    size = "small",
    srcs = ["sol_erda_test.cc"],
    deps = [
        ":sol_erda",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "v_matrix_test",
    size = "small",
    srcs = ["v_matrix_test.cc"],
    deps = [
        ":v_matrix",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hexa_stat",
    srcs = ["hexa_stat.cc"],
    hdrs = ["hexa_stat.h"],
    deps = [
        ":hyper_stats",
        ":sol_erda",
        "//src/combat:damage",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "hexa_stat_test",
    size = "small",
    srcs = ["hexa_stat_test.cc"],
    deps = [
        ":hexa_stat",
        ":hyper_stats",
        ":sol_erda",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hyper_stats",
    srcs = ["hyper_stats.cc"],
    hdrs = ["hyper_stats.h"],
    deps = [
        "//src/character:stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "hyper_stats_test",
    size = "small",
    srcs = ["hyper_stats_test.cc"],
    deps = [
        ":hyper_stats",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hyper_plan",
    srcs = ["hyper_plan.cc"],
    hdrs = ["hyper_plan.h"],
    deps = [
        ":hyper_stats",
        "//src/character",
        "//src/character:stat_preset",
        "//src/protos:character_proto_cc",
    ],
)

cc_test(
    name = "hyper_plan_test",
    srcs = ["hyper_plan_test.cc"],
    deps = [
        ":hyper_plan",
        "//src/character",
        "//src/character:stat_preset",
        "//src/protos:character_proto_cc",
        "@googletest//:gtest_main",
    ],
)
