load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "keybinds_panel",
    srcs = ["keybinds_panel.cc"],
    hdrs = ["keybinds_panel.h"],
    deps = [
        "//src/frontend:keybinds",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:keys",
        "//src/protos:keybinds_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_test(
    name = "keybinds_panel_test",
    size = "small",
    srcs = ["keybinds_panel_test.cc"],
    deps = [
        ":keybinds_panel",
        "//src/frontend:keybinds",
        "//src/frontend/testing:screen_text",
        "//src/protos:keybinds_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "jukebox_panel",
    srcs = ["jukebox_panel.cc"],
    hdrs = ["jukebox_panel.h"],
    deps = [
        "//src:account",
        "//src:game_state",
        "//src:map_level",
        "//src/audio:music_director",
        "//src/audio:track_title",
        "//src/audio:tracks",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/lists:item_menu",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:format",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:dom",
        "@ftxui//:screen",
    ],
)

cc_library(
    name = "options_panel",
    srcs = ["options_panel.cc"],
    hdrs = ["options_panel.h"],
    deps = [
        "//src:account",
        "//src:build_config",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets/text:colors",
        "//src/frontend/widgets/text:keys",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "jukebox_panel_test",
    size = "small",
    srcs = ["jukebox_panel_test.cc"],
    deps = [
        ":jukebox_panel",
        "//src:build_config",
        "//src:game_state",
        "//src/audio:track_title",
        "//src/frontend:placement",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets/text:colors",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "options_panel_test",
    size = "small",
    srcs = ["options_panel_test.cc"],
    deps = [
        ":options_panel",
        "//src:account",
        "//src:build_config",
        "//src/frontend/testing:screen_text",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
