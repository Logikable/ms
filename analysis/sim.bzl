"""A sim is always built optimized.

A tuning tool is run to be read: the whole progression_sim sweep takes minutes
unoptimized. .bazelrc already builds at -c opt, and then the transition below
changes nothing, so a sim shares the game's compiled libraries; it is there
for a command line that asks for another mode.
"""

load("@rules_cc//cc:defs.bzl", "cc_binary")

def _opt_impl(_settings, _attr):
    return {
        "//command_line_option:compilation_mode": "opt",
    }

_opt = transition(
    implementation = _opt_impl,
    inputs = [],
    outputs = ["//command_line_option:compilation_mode"],
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
