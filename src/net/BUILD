load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "frame",
    srcs = ["frame.cc"],
    hdrs = ["frame.h"],
)

cc_test(
    name = "frame_test",
    srcs = ["frame_test.cc"],
    deps = [
        ":frame",
        "@googletest//:gtest_main",
    ],
)
