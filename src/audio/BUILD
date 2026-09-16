load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:embed_audio.bzl", "embedded_audio")

package(default_visibility = ["//visibility:public"])

# The game's music, compiled in. See //bgm for why a fresh clone cannot
# build this and what to do about it.
embedded_audio(
    name = "tracks",
    audio_off = "//src:audio_off",
    empty_src = "tracks_off.cc",
    hdr = "tracks.h",
    tracks = "//bgm:all",
)

cc_library(
    name = "music_player",
    srcs = ["music_player.cc"],
    hdrs = ["music_player.h"],
    deps = [
        ":tracks",
        "//src:build_config",
        "//third_party/miniaudio",
    ],
)

cc_test(
    name = "music_player_test",
    srcs = ["music_player_test.cc"],
    deps = [
        ":music_player",
        ":tracks",
        "//src:account",
        "//src:build_config",
        "@googletest//:gtest_main",
    ],
)
