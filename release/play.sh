#!/bin/sh
# Run this to play, either from a terminal or by opening it from a file
# manager and choosing "Run in Terminal".
#
# The game is the single `ms` file beside this one; this script only makes
# sure it runs from its own directory, whatever the caller's happens to be.
cd "$(dirname "$0")" || exit 1
exec ./ms "$@"
