load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")
load("//tools:release.bzl", "release_binary")

# What a player downloads. Two zips, each holding one self-contained
# executable and a launcher to click:
#
#   bazelisk build //release:all
#
# The platform and the release flags come from the targets rather than the
# command line, so the zips are the same whoever builds them.

package(default_visibility = ["//visibility:public"])

platform(
    name = "linux_x86_64",
    constraint_values = [
        "@platforms//cpu:x86_64",
        "@platforms//os:linux",
    ],
)

release_binary(
    name = "ms_linux",
    binary = "//src:ms",
    platform = ":linux_x86_64",
)

release_binary(
    name = "ms_windows",
    binary = "//src:ms.exe",
    platform = "@llvm_mingw//:windows_x86_64",
)

pkg_files(
    name = "linux_payload",
    srcs = [
        "play.sh",
        ":ms_linux",
    ],
    # Both the game and its launcher have to be runnable straight out of the
    # zip; a zip carries the mode bits, so set them here rather than asking
    # the player to chmod.
    attributes = pkg_attributes(mode = "0755"),
)

pkg_zip(
    name = "ms-linux-x86_64",
    srcs = [":linux_payload"],
)

pkg_files(
    name = "windows_payload",
    srcs = [
        "play.bat",
        ":ms_windows",
    ],
)

pkg_zip(
    name = "ms-windows-x86_64",
    srcs = [":windows_payload"],
)
