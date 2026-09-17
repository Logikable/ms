load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "level_up_card",
    srcs = ["level_up_card.cc"],
    hdrs = ["level_up_card.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "level_up_card_test",
    size = "small",
    srcs = ["level_up_card_test.cc"],
    deps = [
        ":level_up_card",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "offline_card",
    srcs = ["offline_card.cc"],
    hdrs = ["offline_card.h"],
    deps = [
        "//src/combat",
        "//src/combat:offline",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "offline_card_test",
    size = "small",
    srcs = ["offline_card_test.cc"],
    deps = [
        ":offline_card",
        "//src/combat:offline",
        "//src/frontend/widgets:panel_test_base",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "advancement_card",
    srcs = ["advancement_card.cc"],
    hdrs = ["advancement_card.h"],
    deps = [
        "//src/character:job_name",
        "//src/frontend/widgets:chrome",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "advancement_card_test",
    size = "small",
    srcs = ["advancement_card_test.cc"],
    deps = [
        ":advancement_card",
        ":level_up_card",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "death_card",
    srcs = ["death_card.cc"],
    hdrs = ["death_card.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "death_card_test",
    size = "small",
    srcs = ["death_card_test.cc"],
    deps = [
        ":death_card",
        ":level_up_card",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
