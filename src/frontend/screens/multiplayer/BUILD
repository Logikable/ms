load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "player_list_panel",
    srcs = ["player_list_panel.cc"],
    hdrs = ["player_list_panel.h"],
    deps = [
        "//src/character",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/multiplayer:client",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "player_list_panel_test",
    size = "small",
    srcs = ["player_list_panel_test.cc"],
    deps = [
        ":player_list_panel",
        "//src/frontend:types",
        "//src/frontend/testing:screen_text",
        "//src/multiplayer:client",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "party_select_panel",
    srcs = ["party_select_panel.cc"],
    hdrs = ["party_select_panel.h"],
    deps = [
        "//src/character",
        "//src/character:job_name",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/multiplayer:client",
        "//src/multiplayer:protocol",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "party_select_panel_test",
    size = "small",
    srcs = ["party_select_panel_test.cc"],
    deps = [
        ":party_select_panel",
        "//src/frontend:types",
        "//src/frontend/testing:screen_text",
        "//src/multiplayer:client",
        "//src/protos:multiplayer_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)
