#!/bin/bash
# dylibbundler can add the same @loader_path/../Frameworks/ rpath entry
# more than once to a dylib that's reached through more than one parent
# library in a single bundling pass (e.g. OpenEXR, pulled in separately by
# libopencv_imgcodecs and others) - dyld then refuses to load it at all
# ("duplicate LC_RPATH"). This strips every occurrence of that one rpath
# from each binary and re-adds exactly one clean copy.
set -euo pipefail
# An unmatched glob must expand to nothing rather than to the pattern itself:
# with OpenCV linked statically and FFmpeg gone there are no bundled dylibs at
# all, so Contents/Frameworks is empty and "$FRAMEWORKS_DIR"/*.dylib matches
# nothing. Without this, the loop below would run once on the literal pattern.
shopt -s nullglob

BUNDLE_MACOS_DIR="$1"
FRAMEWORKS_DIR="$2"
RPATH="@loader_path/../Frameworks/"

fix_one() {
    local file="$1"
    while install_name_tool -delete_rpath "$RPATH" "$file" >/dev/null 2>&1; do :; done
    install_name_tool -add_rpath "$RPATH" "$file"
}

if [ -d "$BUNDLE_MACOS_DIR" ]; then
    for exe in "$BUNDLE_MACOS_DIR"/*; do
        [ -f "$exe" ] && fix_one "$exe"
    done
fi

if [ -d "$FRAMEWORKS_DIR" ]; then
    for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
        [ -f "$dylib" ] && fix_one "$dylib"
    done
fi

# Exit cleanly even when the last thing attempted was a failed [ -f ] test.
# Under `set -e` the script's exit status is that of its final command, so an
# empty Frameworks directory (nothing to bundle) otherwise fails the build
# with no error message of its own - which is exactly what it used to do.
exit 0
