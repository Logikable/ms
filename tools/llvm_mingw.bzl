"""A hermetic mingw-w64 Clang, for cross-compiling the Windows release here.

llvm-mingw is a full Clang plus the mingw-w64 headers, import libraries and
libc++, all in one tarball. It is fetched and pinned like any other
dependency, so the Windows build needs nothing installed and nothing on a
Windows machine.

This is a repository rule rather than a plain http_archive because the
toolchain has to name its own include directories, and only a repository rule
knows where the archive landed.
"""

_VERSION = "20260616"
_ARCHIVE = "llvm-mingw-{version}-ucrt-{host}-x86_64".format(
    version = _VERSION,
    host = "ubuntu-22.04",
)
_URL = "https://github.com/mstorsjo/llvm-mingw/releases/download/{version}/{archive}.tar.xz".format(
    version = _VERSION,
    archive = _ARCHIVE,
)
_SHA256 = "534b92e067b22a6b4441f48ae9240a3341b17825d04d577eab0cf85c44b4deda"

# The tool paths below are relative to this BUILD file, which is why the
# toolchain is declared inside the archive's own repository. The include
# directories cannot be: Bazel reads those as exec-root paths, so they are
# baked in absolute at fetch time.
#
# A repository made by a module extension carries no repo mapping of its own,
# so every label in the generated file is resolved here -- where the mapping is
# this module's -- and written out canonical.
_CC_DEFS = str(Label("@rules_cc//cc:defs.bzl"))
_CC_TOOLCHAIN_CONFIG = str(
    Label("@rules_cc//cc/private/toolchain:unix_cc_toolchain_config.bzl"),
)
_CPU_X86_64 = str(Label("@platforms//cpu:x86_64"))
_OS_WINDOWS = str(Label("@platforms//os:windows"))
_TOOLCHAIN_TYPE = str(Label("@bazel_tools//tools/cpp:toolchain_type"))

_BUILD = """\
load("{cc_defs}", "cc_toolchain")
load("{cc_toolchain_config}", "cc_toolchain_config")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all_files",
    srcs = glob(
        [
            "bin/**",
            "lib/**",
            "x86_64-w64-mingw32/**",
        ],
        allow_empty = False,
    ),
)

# The resource compiler, named on its own so a genrule can run it. It finds
# libLLVM at $ORIGIN/../lib, so the rest of the archive has to ride along.
filegroup(
    name = "windres",
    srcs = ["bin/x86_64-w64-mingw32-windres"],
    data = [":all_files"],
)

platform(
    name = "windows_x86_64",
    constraint_values = [
        "{cpu_x86_64}",
        "{os_windows}",
    ],
)

cc_toolchain_config(
    name = "config",
    abi_libc_version = "ucrt",
    abi_version = "mingw64",
    compile_flags = [
        "-fcolor-diagnostics",
        "-fno-omit-frame-pointer",
        # Windows 10. FTXUI asks the console for its virtual-terminal modes,
        # which the older headers do not declare.
        "-D_WIN32_WINNT=0x0A00",
        # The wide-character Win32 API. FTXUI refuses to build without it --
        # the console it draws to speaks UTF-16, and the game's box drawing
        # and stars are not ASCII.
        "-DUNICODE",
        "-D_UNICODE",
    ],
    compiler = "clang",
    cpu = "x86_64",
    cxx_builtin_include_directories = {include_dirs},
    cxx_flags = ["-std=c++17"],
    dbg_compile_flags = ["-g"],
    host_system_name = "local",
    link_flags = [
        "-fuse-ld=lld",
        # One file to hand over: the player downloads a zip, unpacks it, and
        # there is nothing beside the executable that it needs.
        "-static",
    ],
    link_libs = ["-lc++"],
    opt_compile_flags = [
        "-O2",
        "-DNDEBUG",
        "-ffunction-sections",
        "-fdata-sections",
    ],
    opt_link_flags = ["-Wl,--gc-sections"],
    supports_start_end_lib = False,
    target_libc = "mingw",
    target_system_name = "x86_64-w64-mingw32",
    tool_paths = {{
        "ar": "bin/x86_64-w64-mingw32-llvm-ar",
        "cpp": "bin/clang-cpp",
        "gcc": "bin/x86_64-w64-mingw32-clang",
        "ld": "bin/x86_64-w64-mingw32-ld",
        "nm": "bin/x86_64-w64-mingw32-nm",
        "objdump": "bin/x86_64-w64-mingw32-objdump",
        "strip": "bin/x86_64-w64-mingw32-strip",
    }},
    toolchain_identifier = "llvm_mingw_x86_64",
    unfiltered_compile_flags = [
        "-no-canonical-prefixes",
        "-Wno-builtin-macro-redefined",
        "-D__DATE__=\\"redacted\\"",
        "-D__TIMESTAMP__=\\"redacted\\"",
        "-D__TIME__=\\"redacted\\"",
    ],
)

cc_toolchain(
    name = "cc_toolchain",
    all_files = ":all_files",
    ar_files = ":all_files",
    compiler_files = ":all_files",
    dwp_files = ":all_files",
    linker_files = ":all_files",
    objcopy_files = ":all_files",
    strip_files = ":all_files",
    toolchain_config = ":config",
)

toolchain(
    name = "toolchain",
    target_compatible_with = [
        "{cpu_x86_64}",
        "{os_windows}",
    ],
    toolchain = ":cc_toolchain",
    toolchain_type = "{toolchain_type}",
)
"""

def _clang_include_dir(repository_ctx, root):
    """Returns the versioned lib/clang/<major>/include, whatever <major> is."""
    versions = repository_ctx.path(root + "/lib/clang").readdir()
    if not versions:
        fail("llvm-mingw archive has no lib/clang directory")
    return str(versions[0]) + "/include"

def _llvm_mingw_impl(repository_ctx):
    repository_ctx.download_and_extract(
        url = _URL,
        sha256 = _SHA256,
        stripPrefix = _ARCHIVE,
    )
    root = str(repository_ctx.path(""))
    include_dirs = [
        root + "/x86_64-w64-mingw32/include",
        root + "/x86_64-w64-mingw32/include/c++/v1",
        _clang_include_dir(repository_ctx, root),
    ]
    repository_ctx.file(
        "BUILD",
        _BUILD.format(
            cc_defs = _CC_DEFS,
            cc_toolchain_config = _CC_TOOLCHAIN_CONFIG,
            cpu_x86_64 = _CPU_X86_64,
            include_dirs = repr(include_dirs),
            os_windows = _OS_WINDOWS,
            toolchain_type = _TOOLCHAIN_TYPE,
        ),
        executable = False,
    )

_llvm_mingw = repository_rule(implementation = _llvm_mingw_impl)

def _extension_impl(_module_ctx):
    _llvm_mingw(name = "llvm_mingw")

llvm_mingw = module_extension(implementation = _extension_impl)
