"""Builds the game's textproto data into the binary as a C++ table."""

load("@rules_cc//cc:defs.bzl", "cc_library")

def _embedded_data_impl(ctx):
    args = ctx.actions.args()
    args.add("--header", ctx.outputs.header_out)
    args.add("--source", ctx.outputs.source_out)
    args.add("--header_include", ctx.outputs.header_out.short_path)

    inputs = []
    for target, symbol in ctx.attr.groups.items():
        files = target.files.to_list()
        inputs += files
        args.add("--group", symbol + "=" + ",".join([f.path for f in files]))

    ctx.actions.run(
        executable = ctx.executable._tool,
        arguments = [args],
        inputs = inputs,
        outputs = [ctx.outputs.header_out, ctx.outputs.source_out],
        mnemonic = "EmbedData",
        progress_message = "Embedding data files into %s" % ctx.label,
    )
    return [DefaultInfo(files = depset([
        ctx.outputs.header_out,
        ctx.outputs.source_out,
    ]))]

_embedded_data = rule(
    implementation = _embedded_data_impl,
    attrs = {
        # Each data directory's filegroup, mapped to the name of the accessor
        # function that will return its contents.
        "groups": attr.label_keyed_string_dict(allow_files = True),
        "header_out": attr.output(),
        "source_out": attr.output(),
        "_tool": attr.label(
            default = "//tools:embed_data",
            executable = True,
            cfg = "exec",
        ),
    },
)

def embedded_data(name, groups, **kwargs):
    """Declares a cc_library holding `groups` as compiled-in string tables.

    Args:
      name: the cc_library's name; the generated header is <name>.h.
      groups: {filegroup label: accessor function name}.
      **kwargs: passed through to the cc_library.
    """
    _embedded_data(
        name = name + "_gen",
        groups = groups,
        header_out = name + ".h",
        source_out = name + ".cc",
    )
    cc_library(
        name = name,
        srcs = [name + ".cc"],
        hdrs = [name + ".h"],
        **kwargs
    )
