load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")
load("//tools:release.bzl", "release_binary")

# What a player downloads. One file per platform, each a self-contained
# executable with nothing beside it to install, configure or click first:
#
#   bazelisk build //release:all
#
# Windows ships the .exe bare. Linux ships it in a zip, because a plain
# download arrives without its executable bit and a zip carries the mode --
# the archive is there for that alone, and holds the one file.
#
# The platform and the release flags come from the targets rather than the
# command line, so what is built is the same whoever builds it.

package(default_visibility = ["//visibility:public"])

# What the file in the zip is dated. A fixed instant rather than the build
# clock, so two builds of the same commit still produce identical archives --
# but a real one, because pkg_zip otherwise stamps everything 1980-01-01, which
# reads as a corrupt download rather than a deliberate default.
#
# 2026-09-03 12:00 UTC, the day this build went out. Noon on purpose: a zip's
# timestamp carries no timezone, so whatever a viewer does with it, midday
# cannot slide onto the day before or after. Move it when the date shown to a
# player would otherwise be misleading.
_RELEASE_TIMESTAMP = 1788436800

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
    srcs = [":ms_linux"],
    # The whole reason there is an archive here: the game has to be runnable
    # straight out of it, rather than after a chmod the player is told about
    # somewhere else.
    attributes = pkg_attributes(mode = "0755"),
)

pkg_zip(
    name = "ms-linux-x86_64",
    srcs = [":linux_payload"],
    timestamp = _RELEASE_TIMESTAMP,
)

# Copied rather than depended on directly so the download lands in
# bazel-bin/release beside the Linux zip, instead of in whatever output
# directory the release transition happened to name.
genrule(
    name = "ms-windows-x86_64",
    srcs = [":ms_windows"],
    outs = ["ms.exe"],
    cmd = "cp $< $@",
)
