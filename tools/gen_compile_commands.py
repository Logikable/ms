#!/usr/bin/env python3
"""Generate compile_commands.json from bazel aquery output."""

import json
import os
import subprocess
import sys

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXECROOT = os.path.join(WORKSPACE, "bazel-ms")
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
        return os.path.join(EXECROOT, path)
    if path.startswith("bazel-out/"):
        return os.path.join(WORKSPACE, path)
    return path


def absolutize_include_args(args):
    """Rewrite relative external/ include paths to absolute paths.

    Handles both separate (-I path) and combined (-Ipath) flag forms.
    bazel-out/ is reachable via the workspace symlink; external/ is not.
    """
    result = []
    separate_flags = {"-iquote", "-I", "-isystem", "-isysroot"}
    combined_prefixes = ["-iquote", "-isystem", "-I"]
    i = 0
    while i < len(args):
        a = args[i]
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
        entries.append({
            "directory": WORKSPACE,
            "file": src if os.path.isabs(src) else os.path.join(WORKSPACE, src),
            "arguments": absolutize_include_args(args),
        })

    out_path = os.path.join(WORKSPACE, "compile_commands.json")
    with open(out_path, "w") as f:
        json.dump(entries, f, indent=2)
    print(f"Wrote {len(entries)} entries to {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
