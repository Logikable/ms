"""Builds the game's music into the binary as .incbin'd blobs and a table."""

load("@rules_cc//cc:defs.bzl", "cc_library")

def _embedded_audio_impl(ctx):
    tracks = ctx.files.tracks
    args = ctx.actions.args()
    args.add("--asm", ctx.outputs.asm_out)
    args.add("--table", ctx.outputs.table_out)
    args.add("--tracks", ",".join([f.path for f in tracks]))

    ctx.actions.run(
        executable = ctx.executable._tool,
        arguments = [args],
        inputs = tracks,
        outputs = [ctx.outputs.asm_out, ctx.outputs.table_out],
        mnemonic = "EmbedAudio",
        progress_message = "Embedding the game's music into %{label}",
    )
    return [DefaultInfo(files = depset([
        ctx.outputs.asm_out,
        ctx.outputs.table_out,
    ]))]

_embedded_audio = rule(
    implementation = _embedded_audio_impl,
    attrs = {
        "tracks": attr.label(allow_files = [".mp3"]),
        "asm_out": attr.output(),
        "table_out": attr.output(),
        "_tool": attr.label(
            default = "//tools:embed_audio",
            executable = True,
            cfg = "exec",
        ),
    },
)

def embedded_audio(name, hdr, tracks, empty_src, audio_off, **kwargs):
    """Declares a cc_library holding `tracks` as compiled-in audio.

    The header is written by hand rather than generated: it is the contract,
    and both the real table and the silent one answer to it.

    Args:
      name: the cc_library's name.
      hdr: the hand-written header both implementations satisfy.
      tracks: filegroup of the .mp3 files to embed.
      empty_src: the .cc standing in for the table when audio is off.
      audio_off: the config_setting selecting a build with no music.
      **kwargs: passed through to the cc_library.
    """
    _embedded_audio(
        name = name + "_gen",
        tracks = tracks,
        asm_out = name + ".S",
        table_out = name + "_table.cc",
    )

    # The .mp3 files are inputs to the COMPILE, not the generator: .incbin
    # reads them when the assembler runs, so they have to be staged beside it.
    cc_library(
        name = name,
        srcs = select({
            audio_off: [empty_src],
            "//conditions:default": [name + ".S", name + "_table.cc"],
        }),
        hdrs = [hdr],
        additional_compiler_inputs = select({
            audio_off: [],
            "//conditions:default": [tracks],
        }),
        **kwargs
    )
