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
    name = "text_columns",
    srcs = ["text_columns.cc"],
    hdrs = ["text_columns.h"],
    deps = ["@ftxui//:screen"],
)

cc_test(
    name = "text_columns_test",
    size = "small",
    srcs = ["text_columns_test.cc"],
    deps = [
        ":format",
        ":text_columns",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "marquee",
    srcs = ["marquee.cc"],
    hdrs = ["marquee.h"],
    deps = [":text_columns"],
)

cc_test(
    name = "marquee_test",
    size = "small",
    srcs = ["marquee_test.cc"],
    deps = [
        ":marquee",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "format",
    srcs = ["format.cc"],
    hdrs = ["format.h"],
    deps = [
        ":text_columns",
        "//src/character/matrix:sol_erda",
    ],
)

cc_test(
    name = "format_test",
    size = "small",
    srcs = ["format_test.cc"],
    deps = [
        ":format",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "game_names",
    srcs = ["game_names.cc"],
    hdrs = ["game_names.h"],
    deps = [
        ":colors",
        ":format",
        "//src/character:inner_ability",
        "//src/character:skill_placement",
        "//src/character:stat_preset",
        "//src/character/account:familiar",
        "//src/character/account:legion",
        "//src/character/matrix:hyper_stats",
        "//src/item",
        "//src/item:flame",
        "//src/item:potential",
        "//src/item:soul",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:equip_set_proto_cc",
        "//src/protos:familiar_proto_cc",
        "//src/protos:legion_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:screen",
    ],
)

cc_test(
    name = "game_names_test",
    size = "small",
    srcs = ["game_names_test.cc"],
    deps = [
        ":colors",
        ":game_names",
        ":text_columns",
        "//src/item:potential",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "keys",
    srcs = ["keys.cc"],
    hdrs = ["keys.h"],
    deps = [
        "@ftxui//:component",
    ],
)

cc_test(
    name = "keys_test",
    size = "small",
    srcs = ["keys_test.cc"],
    deps = [
        ":keys",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "text_field",
    srcs = ["text_field.cc"],
    hdrs = ["text_field.h"],
    deps = [
        ":keys",
        "@ftxui//:component",
    ],
)

cc_test(
    name = "text_field_test",
    size = "small",
    srcs = ["text_field_test.cc"],
    deps = [
        ":text_field",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)
