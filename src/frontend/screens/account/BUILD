load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "familiar_switch_panel",
    srcs = ["familiar_switch_panel.cc"],
    hdrs = ["familiar_switch_panel.h"],
    deps = [
        "//src/character:familiar",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:familiar_list",
        "//src/frontend/widgets:keys",
        "//src/protos:familiar_proto_cc",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/types:span",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "familiar_switch_panel_test",
    size = "small",
    srcs = ["familiar_switch_panel_test.cc"],
    deps = [
        ":familiar_switch_panel",
        "//src/character:familiar",
        "//src/frontend/testing:panel_test_base",
        "@ftxui//:component",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "familiar_card",
    srcs = ["familiar_card.cc"],
    hdrs = ["familiar_card.h"],
    deps = [
        "//src/character:familiar",
        "//src/frontend/screens/inspect:inspect_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:scroll_card",
        "//src/protos:familiar_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "familiar_card_test",
    size = "small",
    srcs = ["familiar_card_test.cc"],
    deps = [
        ":familiar_card",
        "//src/character:familiar",
        "//src/frontend/testing:panel_test_base",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "dailies_panel",
    srcs = ["dailies_panel.cc"],
    hdrs = ["dailies_panel.h"],
    deps = [
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:confirm_prompt",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:text_columns",
        "@ftxui//:component",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "dailies_panel_test",
    size = "small",
    srcs = ["dailies_panel_test.cc"],
    deps = [
        ":dailies_panel",
        "//src/frontend/testing:screen_text",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "legion_panel",
    srcs = ["legion_panel.cc"],
    hdrs = ["legion_panel.h"],
    deps = [
        "//src:game_state",
        "//src/character",
        "//src/character:legion",
        "//src/character:stat_preset",
        "//src/frontend:types",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
        "//src/protos:save_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "legion_panel_test",
    size = "small",
    srcs = ["legion_panel_test.cc"],
    deps = [
        ":legion_panel",
        "//src:game_state",
        "//src/character:legion",
        "//src/character:stat_preset",
        "//src/frontend/testing:screen_text",
        "//src/protos:character_proto_cc",
        "//src/protos:legion_proto_cc",
        "//src/protos:save_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "link_skill_panel",
    srcs = ["link_skill_panel.cc"],
    hdrs = ["link_skill_panel.h"],
    deps = [
        "//src/character",
        "//src/character:character_stats",
        "//src/character:job_branch",
        "//src/character:link",
        "//src/character:stat_preset",
        "//src/frontend:types",
        "//src/frontend/screens/inspect:skill_inspect_panel",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:game_names",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "link_skill_panel_test",
    size = "small",
    srcs = ["link_skill_panel_test.cc"],
    deps = [
        ":link_skill_panel",
        "//src/character:link",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/testing:screen_text",
        "//src/protos:character_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "character_select_panel",
    srcs = ["character_select_panel.cc"],
    hdrs = ["character_select_panel.h"],
    deps = [
        "//src:game_state",
        "//src:roster",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:job_name",
        "//src/frontend:panel_widths",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:item_menu",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:stat_rows",
        "//src/frontend/widgets:text_columns",
        "@ftxui//:dom",
    ],
)

cc_test(
    name = "character_select_panel_test",
    size = "small",
    srcs = ["character_select_panel_test.cc"],
    deps = [
        ":character_select_panel",
        "//src:game_state",
        "//src:roster",
        "//src/character",
        "//src/character:character_stats",
        "//src/character:hyper_stats",
        "//src/character:stat_preset",
        "//src/frontend:placement",
        "//src/frontend/testing:screen_text",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:stat_rows",
        "//src/protos:character_proto_cc",
        "//src/protos:save_proto_cc",
        "//src/protos:skill_proto_cc",
        "@ftxui//:dom",
        "@ftxui//:screen",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "achievements_panel",
    srcs = ["achievements_panel.cc"],
    hdrs = ["achievements_panel.h"],
    deps = [
        "//src:achievement",
        "//src/frontend/widgets:chrome",
        "//src/frontend/widgets:colors",
        "//src/frontend/widgets:format",
        "//src/frontend/widgets:keys",
        "//src/frontend/widgets:marquee",
        "//src/protos:achievement_proto_cc",
        "@ftxui//:component",
        "@ftxui//:dom",
        "@protobuf",
    ],
)

cc_test(
    name = "achievements_panel_test",
    size = "small",
    srcs = ["achievements_panel_test.cc"],
    deps = [
        ":achievements_panel",
        "//src:achievement",
        "//src/frontend/testing:panel_test_base",
        "//src/frontend/widgets:colors",
        "//src/protos:achievement_proto_cc",
        "@ftxui//:component",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
