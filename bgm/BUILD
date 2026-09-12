package(default_visibility = ["//visibility:public"])

# The game's music, compiled into the binary by //src/audio:tracks.
#
# The .mp3 files themselves are NOT in the repo -- they are extracted from an
# installed MapleStory client by tools/wz/bgm.py, and .gitignore keeps them
# out. A fresh clone therefore has none, and a build that wants music will
# fail here saying so; build with --define=audio=off for one that does not.
filegroup(
    name = "all",
    srcs = glob(["*.mp3"]),
)
