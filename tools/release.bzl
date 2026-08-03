"""Builds a binary the way a release wants it, whatever flags were asked for.

A release build is optimised and statically linked, and it targets a platform
named here rather than whichever machine is doing the building. Putting that
in a transition rather than a --config means the zips are the same whoever
builds them and whatever they type.
"""

def _release_transition_impl(settings, attr):
    features = list(settings["//command_line_option:features"])
    if "fully_static_link" not in features:
        features.append("fully_static_link")
    return {
        "//command_line_option:compilation_mode": "opt",
        "//command_line_option:features": features,
        "//command_line_option:platforms": [str(attr.platform)],
    }

_release_transition = transition(
    implementation = _release_transition_impl,
    inputs = ["//command_line_option:features"],
    outputs = [
        "//command_line_option:compilation_mode",
        "//command_line_option:features",
        "//command_line_option:platforms",
    ],
)

def _release_binary_impl(ctx):
    # The executable alone: a cc_binary's default files carry runfiles symlinks
    # and .params along with it, and none of that belongs in a zip.
    return [DefaultInfo(files = depset([ctx.executable.binary]))]

release_binary = rule(
    implementation = _release_binary_impl,
    doc = "The given cc_binary, built optimised and static for `platform`.",
    attrs = {
        "binary": attr.label(
            mandatory = True,
            executable = True,
            cfg = _release_transition,
        ),
        "platform": attr.label(
            mandatory = True,
            doc = "The platform to build for, e.g. //release:linux_x86_64.",
        ),
    },
)
