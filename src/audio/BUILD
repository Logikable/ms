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
    name = "jukebox",
    srcs = ["jukebox.cc"],
    hdrs = ["jukebox.h"],
    deps = [
        ":tracks",
        "//third_party/miniaudio",
    ],
)

cc_test(
    name = "jukebox_test",
    srcs = ["jukebox_test.cc"],
    deps = [
        ":jukebox",
        ":tracks",
        "@googletest//:gtest_main",
    ],
)
