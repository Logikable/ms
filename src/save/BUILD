load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "save_migration",
    srcs = ["save_migration.cc"],
    hdrs = ["save_migration.h"],
    deps = [
        "//src/item",
        "//src/item:currency",
        "//src/protos:account_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:save_proto_cc",
        "@protobuf",
    ],
)

cc_library(
    name = "save",
    srcs = ["save.cc"],
    hdrs = ["save.h"],
    deps = [
        ":save_migration",
        "//src:account",
        "//src:game_state",
        "//src:roster",
        "//src/item",
        "//src/protos:save_proto_cc",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "save_lock",
    srcs = ["save_lock.cc"],
    hdrs = ["save_lock.h"],
)

cc_test(
    name = "save_lock_test",
    size = "small",
    srcs = ["save_lock_test.cc"],
    deps = [
        ":save_lock",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "save_test",
    size = "small",
    srcs = ["save_test.cc"],
    deps = [
        ":save",
        "//src:game_state",
        "//src:roster",
        "//src/character",
        "//src/character/matrix:hyper_stats",
        "//src/item:equip_instance",
        "//src/protos:achievement_proto_cc",
        "//src/protos:character_proto_cc",
        "//src/protos:equip_proto_cc",
        "//src/protos:item_proto_cc",
        "//src/protos:keybinds_proto_cc",
        "//src/protos:save_proto_cc",
        "//src/protos:skill_proto_cc",
        "@googletest//:gtest_main",
    ],
)
