"""A sim is always built optimized.

A tuning tool is run to be read, and at the default -O1 that means waiting: the
whole progression_sim sweep takes minutes unoptimized and 26 seconds optimized.
Rather than rely on everyone remembering a flag, each sim carries the setting
itself. The transition below builds the binary and its dependencies at -c opt
whatever the command line says, leaving the game's own tests on the default.
"""

load("@rules_cc//cc:defs.bzl", "cc_binary")

def _opt_impl(settings, _attr):
    return {
        "//command_line_option:compilation_mode": "opt",
        # .bazelrc's -O1 is for the default build. Left in, it would come
        # after opt's -O2 and override it.
        "//command_line_option:copt": [
            c
            for c in settings["//command_line_option:copt"]
            if c != "-O1"
        ],
    }

_opt = transition(
    implementation = _opt_impl,
    inputs = ["//command_line_option:copt"],
    outputs = [
        "//command_line_option:compilation_mode",
        "//command_line_option:copt",
    ],
)

def _optimized_impl(ctx):
    built = ctx.attr.binary[0]
    out = ctx.actions.declare_file(ctx.label.name)

    # A symlink rather than a copy: it runs the binary built under the
    # transition, under the name the sim was declared with.
    ctx.actions.symlink(
        output = out,
        target_file = built[DefaultInfo].files_to_run.executable,
        is_executable = True,
    )
    return [DefaultInfo(
        executable = out,
        runfiles = built[DefaultInfo].default_runfiles,
    )]

_optimized = rule(
    implementation = _optimized_impl,
    attrs = {"binary": attr.label(cfg = _opt, mandatory = True)},
    executable = True,
)

def sim_binary(name, **kwargs):
    """A cc_binary that is always built optimized.

    Args:
      name: the name the sim is run by.
      **kwargs: passed to the underlying cc_binary.
    """

    # Tagged manual so a wildcard build reaches the sim only through the wrapper
    # and never builds the same sources twice.
    cc_binary(name = name + "_unoptimized", tags = ["manual"], **kwargs)
    _optimized(name = name, binary = ":" + name + "_unoptimized")
