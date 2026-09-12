load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

licenses(["unencumbered"])

exports_files(["LICENSE"])

# Audio playback for the jukebox. Vendored rather than fetched so a build
# needs no network, and chosen over SDL/OpenAL because it asks nothing of the
# build: it decodes MP3 itself and dlopens libpulse/libasound on Linux, so
# there is no system library for the hermetic toolchain to find and no mingw
# import lib for the Windows cross-build.
#
# Only MP3 decoding is turned on. The formats we do not ship are switched off
# because every one of them is compile time we would otherwise pay on a
# four-megabyte header.
cc_library(
    name = "miniaudio",
    srcs = ["miniaudio.c"],
    hdrs = ["miniaudio.h"],
    defines = [
        "MA_NO_ENCODING",
        "MA_NO_WAV",
        "MA_NO_FLAC",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-lm",
            "-ldl",
        ],
    }),
)
