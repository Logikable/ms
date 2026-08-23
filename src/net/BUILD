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

cc_library(
    name = "socket",
    srcs = ["socket.cc"],
    hdrs = ["socket.h"],
    linkopts = select({
        "@platforms//os:windows": ["-lws2_32"],
        "//conditions:default": [],
    }),
    deps = ["@abseil-cpp//absl/log"],
)

cc_test(
    name = "socket_test",
    srcs = ["socket_test.cc"],
    deps = [
        ":socket",
        "@googletest//:gtest_main",
    ],
)
