load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "equip_proto",
    srcs = ["equip.proto"],
)

cc_proto_library(
    name = "equip_proto_cc",
    deps = [":equip_proto"],
)

proto_library(
    name = "scroll_proto",
    srcs = ["scroll.proto"],
    deps = [":equip_proto"],
)

cc_proto_library(
    name = "scroll_proto_cc",
    deps = [":scroll_proto"],
)

proto_library(
    name = "character_proto",
    srcs = ["character.proto"],
    deps = [
        ":equip_proto",
        ":item_proto",
    ],
)

cc_proto_library(
    name = "character_proto_cc",
    deps = [":character_proto"],
)

proto_library(
    name = "keybinds_proto",
    srcs = ["keybinds.proto"],
)

cc_proto_library(
    name = "keybinds_proto_cc",
    deps = [":keybinds_proto"],
)

proto_library(
    name = "boss_proto",
    srcs = ["boss.proto"],
    deps = [":mob_proto"],
)

cc_proto_library(
    name = "boss_proto_cc",
    deps = [":boss_proto"],
)

proto_library(
    name = "map_proto",
    srcs = ["map.proto"],
    deps = [":mob_proto"],
)

cc_proto_library(
    name = "map_proto_cc",
    deps = [":map_proto"],
)

proto_library(
    name = "mob_proto",
    srcs = ["mob.proto"],
)

cc_proto_library(
    name = "mob_proto_cc",
    deps = [":mob_proto"],
)

proto_library(
    name = "item_proto",
    srcs = ["item.proto"],
)

cc_proto_library(
    name = "item_proto_cc",
    deps = [":item_proto"],
)

proto_library(
    name = "skill_proto",
    srcs = ["skill.proto"],
    deps = [
        ":character_proto",
        ":equip_proto",
    ],
)

cc_proto_library(
    name = "skill_proto_cc",
    deps = [":skill_proto"],
)

proto_library(
    name = "equip_set_proto",
    srcs = ["equip_set.proto"],
    deps = [
        ":equip_proto",
        ":skill_proto",
    ],
)

cc_proto_library(
    name = "equip_set_proto_cc",
    deps = [":equip_set_proto"],
)

proto_library(
    name = "save_proto",
    srcs = ["save.proto"],
    deps = [
        ":character_proto",
        ":keybinds_proto",
    ],
)

cc_proto_library(
    name = "save_proto_cc",
    deps = [":save_proto"],
)
