#!/usr/bin/env python3
"""Generate compile_commands.json from bazel aquery output."""

import json
import os
import subprocess
import sys

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Stable, gitignored symlink to the output base's external/ tree. The execroot
# (bazel-ms/external) only holds the repos Bazel happened to symlink there, so it
# is missing repos like ftxui; the output base holds the complete set. main()
# points this at $(bazel info output_base)/external.
EXTERNAL = os.path.join(WORKSPACE, "bazel-external")
TARGETS = ["//src/..."]


def path_fragments(data):
    """Build id -> path map from pathFragments."""
    fragments = {f["id"]: f for f in data["pathFragments"]}
    cache = {}

    def resolve(fid):
        if fid in cache:
            return cache[fid]
        f = fragments[fid]
        label = f["label"]
        parent = f.get("parentId")
        path = (resolve(parent) + "/" + label) if parent else label
        cache[fid] = path
        return path

    for fid in fragments:
        resolve(fid)
    return cache


def artifact_paths(data, fragments):
    return {a["id"]: fragments[a["pathFragmentId"]] for a in data["artifacts"]}


def source_file(args):
    """Extract the .cc/.cpp source file from compiler arguments."""
    for a in reversed(args):
        if a.endswith(".cc") or a.endswith(".cpp") or a.endswith(".c"):
            return a
    return None


def absolutize_path(path):
    """Rewrite a relative include path to an absolute one."""
    if os.path.isabs(path):
        return path
    if path.startswith("external/"):
        return os.path.join(EXTERNAL, path[len("external/"):])
    if path.startswith("bazel-out/"):
        return os.path.join(WORKSPACE, path)
    return path


def ensure_external_symlink():
    """Point WORKSPACE/bazel-external at the output base's external/ tree."""
    result = subprocess.run(
        ["bazelisk", "info", "output_base"],
        capture_output=True, text=True, cwd=WORKSPACE
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(1)
    target = os.path.join(result.stdout.strip(), "external")
    if os.path.islink(EXTERNAL) and os.readlink(EXTERNAL) == target:
        return
    if os.path.islink(EXTERNAL) or os.path.exists(EXTERNAL):
        os.remove(EXTERNAL)
    os.symlink(target, EXTERNAL)


def absolutize_include_args(args):
    """Rewrite relative external/ include paths to absolute paths.

    Handles both separate (-I path) and combined (-Ipath) flag forms.
    bazel-out/ is reachable via the workspace symlink; external/ is not.
    """
    result = []
    # The hermetic toolchain passes its libc++, resource, and builtin-header
    # paths via these flags (not just -I); all must be absolute or clangd's
    # -nostdinc++ leaves the standard library unresolved.
    separate_flags = {"-iquote", "-I", "-isystem", "-isysroot", "-cxx-isystem",
                      "-idirafter", "-imacros", "-resource-dir"}
    combined_prefixes = ["-iquote", "-isystem", "-idirafter", "-I", "-B"]
    i = 0
    while i < len(args):
        a = args[i]
        # -nostdinc++ suppresses clang's default C++ search, but the toolchain's
        # explicit -isystem dirs then sit before its libc++ headers, breaking
        # libc++'s <stdint.h> include chain under clangd. Dropping it lets clang
        # order libc++ (still found via -cxx-isystem) correctly. Editor-only;
        # the real build keeps the flag.
        if a == "-nostdinc++":
            i += 1
            continue
        if a in separate_flags and i + 1 < len(args):
            result.append(a)
            i += 1
            result.append(absolutize_path(args[i]))
        else:
            rewritten = False
            for prefix in combined_prefixes:
                if a.startswith(prefix) and len(a) > len(prefix):
                    result.append(prefix + absolutize_path(a[len(prefix):]))
                    rewritten = True
                    break
            if not rewritten:
                result.append(a)
        i += 1
    return result


def main():
    ensure_external_symlink()
    print("Running bazel aquery...", file=sys.stderr)
    result = subprocess.run(
        ["bazelisk", "aquery", "--output=jsonproto",
         "mnemonic(\"CppCompile\", {})".format(" + ".join(TARGETS))],
        capture_output=True, text=True, cwd=WORKSPACE
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(1)

    data = json.loads(result.stdout)
    fragments = path_fragments(data)
    artifacts = artifact_paths(data, fragments)

    entries = []
    for action in data.get("actions", []):
        if action.get("mnemonic") != "CppCompile":
            continue
        args = action["arguments"]
        src = source_file(args)
        if not src:
            continue
        abs_args = absolutize_include_args(args)
        if abs_args:
            # args[0] is the compiler (a relative external/ path to the hermetic
            # cc_wrapper); absolutize so editors/clangd can probe its includes.
            abs_args[0] = absolutize_path(abs_args[0])
        entries.append({
            "directory": WORKSPACE,
            "file": src if os.path.isabs(src) else os.path.join(WORKSPACE, src),
            "arguments": abs_args,
        })

    out_path = os.path.join(WORKSPACE, "compile_commands.json")
    with open(out_path, "w") as f:
        json.dump(entries, f, indent=2)
    print(f"Wrote {len(entries)} entries to {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
