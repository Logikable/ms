load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "reroll_prompt",
    srcs = ["reroll_prompt.cc"],
    hdrs = ["reroll_prompt.h"],
    deps = [
        ":confirm_prompt",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_library(
    name = "confirm_prompt",
    srcs = ["confirm_prompt.cc"],
    hdrs = ["confirm_prompt.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "confirm_prompt_test",
    size = "small",
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
    name = "continue_prompt",
    srcs = ["continue_prompt.cc"],
    hdrs = ["continue_prompt.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "continue_prompt_test",
    size = "small",
    srcs = ["continue_prompt_test.cc"],
    deps = [
        ":continue_prompt",
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
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "amount_selector_test",
    size = "small",
    srcs = ["amount_selector_test.cc"],
    deps = [
        ":amount_selector",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
