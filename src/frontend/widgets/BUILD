load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "exp_bar",
    srcs = ["exp_bar.cc"],
    hdrs = ["exp_bar.h"],
    deps = [
        ":chrome",
        "//src/character:exp_table",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "exp_bar_test",
    size = "small",
    srcs = ["exp_bar_test.cc"],
    deps = [
        ":exp_bar",
        "//src/character:exp_table",
        "//src/frontend/widgets/text:colors",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "buff_dots",
    srcs = ["buff_dots.cc"],
    hdrs = ["buff_dots.h"],
)

cc_test(
    name = "buff_dots_test",
    size = "small",
    srcs = ["buff_dots_test.cc"],
    deps = [
        ":buff_dots",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "chrome",
    srcs = ["chrome.cc"],
    hdrs = ["chrome.h"],
    deps = [
        ":buff_dots",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:marquee",
        "//src/item",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:mob_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_test(
    name = "chrome_test",
    size = "small",
    srcs = ["chrome_test.cc"],
    deps = [
        ":chrome",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/lists:item_row",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
