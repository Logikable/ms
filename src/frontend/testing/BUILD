load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "screen_text",
    testonly = True,
    srcs = ["screen_text.cc"],
    hdrs = ["screen_text.h"],
    deps = ["@ftxui//:screen"],
)

cc_test(
    name = "screen_text_test",
    size = "small",
    srcs = ["screen_text_test.cc"],
    deps = [
        ":screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "panel_test_base",
    testonly = True,
    hdrs = ["panel_test_base.h"],
    deps = [
        ":screen_text",
        "//src:account",
        "//src/character",
        "//src/character:exp_table",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest",
    ],
)
