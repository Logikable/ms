"""A sim is always built optimized.

A tuning tool is run to be read, and at the default -O0 that means waiting: the
whole progression_sim sweep takes minutes unoptimized and 26 seconds
optimized. Rather than ask everyone to remember a flag, each sim
carries the setting itself -- the transition below builds the binary and
everything under it at -c opt whatever the command line said, leaving the
game's own tests on the default.
"""

load("@rules_cc//cc:defs.bzl", "cc_binary")

def _opt_impl(_settings, _attr):
    return {"//command_line_option:compilation_mode": "opt"}

_opt = transition(
    implementation = _opt_impl,
    inputs = [],
    outputs = ["//command_line_option:compilation_mode"],
)

def _optimized_impl(ctx):
    built = ctx.attr.binary[0]
    out = ctx.actions.declare_file(ctx.label.name)

    # A symlink rather than a copy: what runs is the binary built under the
    # transition, reached by the name the sim was declared with.
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

    # Tagged manual so a wildcard build reaches the sim through the wrapper
    # only, and never builds the same sources twice.
    cc_binary(name = name + "_unoptimized", tags = ["manual"], **kwargs)
    _optimized(name = name, binary = ":" + name + "_unoptimized")
