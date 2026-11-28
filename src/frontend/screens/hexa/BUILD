load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "hexa_stat_activate_panel",
    srcs = ["hexa_stat_activate_panel.cc"],
    hdrs = ["hexa_stat_activate_panel.h"],
    deps = [
        "//src/character/matrix:hexa_stat",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:keys",
        "//src/protos:character_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "hexa_stat_activate_panel_test",
    size = "small",
    srcs = ["hexa_stat_activate_panel_test.cc"],
    deps = [
        ":hexa_stat_activate_panel",
        "//src/frontend/testing:screen_text",
        "//src/protos:character_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "hexa_stat_card",
    srcs = ["hexa_stat_card.cc"],
    hdrs = ["hexa_stat_card.h"],
    deps = [
        "//src/character/matrix:hexa_stat",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:game_names",
        "//src/frontend/widgets/text:text_columns",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "hexa_stat_card_test",
    size = "small",
    srcs = ["hexa_stat_card_test.cc"],
    deps = [
        ":hexa_stat_card",
        "//src/frontend/testing:screen_text",
        "//src/protos:character_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
