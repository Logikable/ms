load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "colors",
    hdrs = ["colors.h"],
    deps = [
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "panel_util",
    srcs = ["panel_util.cc"],
    hdrs = ["panel_util.h"],
    deps = [
        ":colors",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_test(
    name = "panel_util_test",
    srcs = ["panel_util_test.cc"],
    deps = [
        ":colors",
        ":panel_util",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "confirm_prompt",
    srcs = ["confirm_prompt.cc"],
    hdrs = ["confirm_prompt.h"],
    deps = [
        ":panel_util",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "confirm_prompt_test",
    srcs = ["confirm_prompt_test.cc"],
    deps = [
        ":confirm_prompt",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "amount_selector",
    srcs = ["amount_selector.cc"],
    hdrs = ["amount_selector.h"],
    deps = [
        ":confirm_prompt",
        ":panel_util",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "amount_selector_test",
    srcs = ["amount_selector_test.cc"],
    deps = [
        ":amount_selector",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "item_menu",
    srcs = ["item_menu.cc"],
    hdrs = ["item_menu.h"],
    deps = [
        ":panel_util",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "item_menu_test",
    srcs = ["item_menu_test.cc"],
    deps = [
        ":item_menu",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "panel_test_base",
    testonly = True,
    hdrs = ["panel_test_base.h"],
    deps = [
        "//src/character",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest",
    ],
)
