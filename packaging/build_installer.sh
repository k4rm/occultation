#!/usr/bin/env bash
#
# Build a distributable macOS .pkg installer for Occultation.
#
# Takes the bundles an ordinary `cmake --build` already produced and wraps them
# into one flat, double-clickable installer: a pkgbuild component package per
# format, combined by productbuild into a single choice-driven product archive
# that drops the VST3/AU into /Library/Audio/Plug-Ins and the standalone app
# into /Applications — no drag-and-drop, no Finder instructions.
#
#   ./packaging/build_installer.sh                      # unsigned, for local testing
#   ./packaging/build_installer.sh --sign-app "Developer ID Application: … (TEAMID)" \
#                                 --sign-pkg "Developer ID Installer: … (TEAMID)" \
#                                 --notarize <keychain-profile>
#
# Or through CMake, which passes --build-dir/--version for you:
#
#   cmake --build build --target installer
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build"
CONFIG="Release"
VERSION="1.0.0"
IDENTIFIER_BASE="com.apokrypsi.occultation"
SIGN_APP=""          # "Developer ID Application: …" — signs the bundles
SIGN_PKG=""          # "Developer ID Installer: …"   — signs the installer
NOTARIZE_PROFILE=""  # notarytool --keychain-profile name
OUTPUT=""

usage() {
    # The header comment above, up to the first line of actual code.
    awk 'NR > 2 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"
    cat <<'EOF'

Options:
  --build-dir <dir>     CMake build directory (default: <repo>/build)
  --config <cfg>        Build config subdirectory of the artefacts (default: Release)
  --version <x.y.z>     Version stamped into the packages (default: 1.0.0)
  --sign-app <identity> Codesign the bundles with this Developer ID Application identity
  --sign-pkg <identity> Sign the installer with this Developer ID Installer identity
  --notarize <profile>  Submit to notarytool using this stored keychain profile, then staple
  --output <file>       Installer path (default: <build-dir>/installer/Occultation-<version>.pkg)
  -h, --help            This message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --config)    CONFIG="$2"; shift 2 ;;
        --version)   VERSION="$2"; shift 2 ;;
        --sign-app)  SIGN_APP="$2"; shift 2 ;;
        --sign-pkg)  SIGN_PKG="$2"; shift 2 ;;
        --notarize)  NOTARIZE_PROFILE="$2"; shift 2 ;;
        --output)    OUTPUT="$2"; shift 2 ;;
        -h|--help)   usage; exit 0 ;;
        *) echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
ARTEFACTS="${BUILD_DIR}/OccultationPlugin_artefacts/${CONFIG}"
WORK="${BUILD_DIR}/installer"
STAGE="${WORK}/stage"
PKGS="${WORK}/components"
RESOURCES="${WORK}/resources"
OUTPUT="${OUTPUT:-${WORK}/Occultation-${VERSION}.pkg}"
[[ "$OUTPUT" = /* ]] || OUTPUT="${PWD}/${OUTPUT}"

VST3_SRC="${ARTEFACTS}/VST3/Occultation.vst3"
AU_SRC="${ARTEFACTS}/AU/Occultation.component"
APP_SRC="${ARTEFACTS}/Standalone/Occultation.app"

for src in "$VST3_SRC" "$AU_SRC" "$APP_SRC"; do
    if [[ ! -d "$src" ]]; then
        echo "error: missing $src" >&2
        echo "       build first: cmake --build \"${BUILD_DIR}\" -j 8" >&2
        exit 1
    fi
done

# --- What the built binary actually supports -----------------------------
# Read the architectures and the minimum OS out of the Mach-O instead of
# hardcoding them, so the installer's own gate can never claim compatibility
# the binary doesn't have (and needs no edit if the build settings change).
#
# It has to be every Mach-O the bundle *ships*, not just the plugin binary.
# dyld enforces LC_BUILD_VERSION on each image it loads, so one bundled
# Homebrew dylib built for the build machine's macOS raises the real floor for
# the whole plugin even when our own binary was compiled for something older —
# the floor is the maximum over all of them, and the usable architectures are
# the intersection.
VST3_BIN="${VST3_SRC}/Contents/MacOS/Occultation"

macho_minos() {
    local v
    v="$(otool -l "$1" | awk '/LC_BUILD_VERSION/{f=1} f && /^ *minos/{print $2; exit}')"
    # Older toolchains emit LC_VERSION_MIN_MACOSX instead of LC_BUILD_VERSION.
    [[ -n "$v" ]] || v="$(otool -l "$1" | awk '/LC_VERSION_MIN_MACOSX/{f=1} f && /^ *version/{print $2; exit}')"
    echo "$v"
}

# Sort -V puts the highest version last; good enough for macOS's x.y[.z].
version_max() { printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1; }

MIN_OS=""
MIN_OS_SOURCE=""
HOST_ARCHS=""
while IFS= read -r macho; do
    v="$(macho_minos "$macho")"
    if [[ -n "$v" && "$(version_max "${MIN_OS:-0}" "$v")" == "$v" && "$v" != "${MIN_OS:-}" ]]; then
        MIN_OS="$v"
        MIN_OS_SOURCE="$(basename "$macho")"
    fi

    archs="$(lipo -archs "$macho" 2>/dev/null)" || continue
    if [[ -z "$HOST_ARCHS" ]]; then
        HOST_ARCHS="$archs"
    else
        kept=""
        for a in $HOST_ARCHS; do [[ " $archs " == *" $a "* ]] && kept="${kept}${kept:+ }${a}"; done
        HOST_ARCHS="$kept"
    fi
done < <(
    printf '%s\n' "$VST3_BIN"
    find "${VST3_SRC}/Contents/Frameworks" -type f \( -name '*.dylib' -o -name '*.so' \) 2>/dev/null
)

: "${MIN_OS:=11.0}"
if [[ -z "$HOST_ARCHS" ]]; then
    echo "error: the bundled dylibs share no architecture with the plugin binary" >&2
    exit 1
fi
HOST_ARCHS="$(echo "$HOST_ARCHS" | tr ' ' ',')"

echo "==> Occultation ${VERSION}  (${HOST_ARCHS}, macOS ${MIN_OS}+)"

# Both of these are inherited from the dependency stack, not from a flag on this
# project: Homebrew's bottles (FFmpeg, and whatever OpenCV was built against)
# are arm64-only and built for the build machine's macOS. Setting
# CMAKE_OSX_DEPLOYMENT_TARGET/CMAKE_OSX_ARCHITECTURES on Occultation alone
# would only make its *own* binary claim a wider range while the bundled
# dylibs still refuse to load — so flag it honestly instead of suggesting a
# one-line fix that isn't one. See the README's packaging section.
if [[ "${MIN_OS%%.*}" -ge 13 ]]; then
    echo "    WARNING: requires macOS ${MIN_OS} or newer (floor set by ${MIN_OS_SOURCE}), which"
    echo "             excludes most users. Lowering it means rebuilding OpenCV *and* FFmpeg"
    echo "             with CMAKE_OSX_DEPLOYMENT_TARGET set, then rebuilding this — see README."
fi
if [[ "$HOST_ARCHS" != *x86_64* ]]; then
    echo "    NOTE: Apple Silicon only (${HOST_ARCHS}). A universal build needs universal"
    echo "          OpenCV and FFmpeg first; Homebrew's bottles are single-arch."
fi

rm -rf "$WORK"
mkdir -p "${STAGE}/vst3" "${STAGE}/au" "${STAGE}/app" "$PKGS" "$RESOURCES"

# --- Stage the bundles ---------------------------------------------------
# ditto, not cp: it preserves symlinks, resource forks and the code signature's
# extended attributes, which a plain recursive copy can quietly mangle. One
# staging root per component, since each goes to a different install location.
echo "==> Staging bundles"
ditto "$VST3_SRC" "${STAGE}/vst3/Occultation.vst3"
ditto "$AU_SRC"   "${STAGE}/au/Occultation.component"
ditto "$APP_SRC"  "${STAGE}/app/Occultation.app"

# Strip quarantine and any other xattrs picked up along the way — a payload
# carrying com.apple.quarantine installs a quarantined plugin, which hosts then
# refuse to load. Do this before signing: clearing xattrs invalidates the
# signature, not the other way round.
xattr -cr "${STAGE}"

# --- Optional Developer ID signing --------------------------------------
# The build's own post-build step ad-hoc signs (`--sign -`), which is enough to
# run locally but leaves Gatekeeper blocking anyone who downloads it. A real
# identity signs inside-out: every nested dylib first, then the bundle, with the
# hardened runtime and a secure timestamp, both of which notarisation requires.
sign_bundle() {
    local bundle="$1"
    [[ -n "$SIGN_APP" ]] || return 0
    find "${bundle}/Contents/Frameworks" -type f \( -name '*.dylib' -o -name '*.so' \) -print0 2>/dev/null \
        | xargs -0 -r codesign --force --options runtime --timestamp --sign "$SIGN_APP" || true
    codesign --force --options runtime --timestamp \
             --entitlements "${SCRIPT_DIR}/hardened.entitlements" \
             --sign "$SIGN_APP" "$bundle"
    codesign --verify --deep --strict --verbose=1 "$bundle"
}

if [[ -n "$SIGN_APP" ]]; then
    echo "==> Signing bundles as: ${SIGN_APP}"
    sign_bundle "${STAGE}/vst3/Occultation.vst3"
    sign_bundle "${STAGE}/au/Occultation.component"
    sign_bundle "${STAGE}/app/Occultation.app"
else
    echo "==> Not signing bundles (ad-hoc signatures from the build are kept)"
    echo "    Downloads of an unsigned installer hit Gatekeeper; pass --sign-app/--sign-pkg to fix."
fi

# --- One component package per format ------------------------------------
# --analyze first, then force BundleIsRelocatable=false. pkgbuild defaults
# bundles to relocatable, which means Installer will happily redirect the
# payload to wherever an older copy of the bundle already sits (a stale
# ~/Library plugin, a Downloads folder) instead of the install-location asked
# for here — the classic "installer said success, host sees nothing" bug.
build_component() {
    local name="$1" root="$2" identifier="$3" location="$4"
    local plist="${PKGS}/${name}.plist"

    pkgbuild --analyze --root "$root" "$plist" >/dev/null
    # --analyze only emits the keys it considers non-default, so set them
    # explicitly on every bundle it found rather than trusting the defaults:
    # relocatable off (see above), and version-checking off so an existing
    # newer-versioned copy can't make Installer silently skip the payload.
    local count
    count=$(/usr/libexec/PlistBuddy -c "Print" "$plist" | grep -c RootRelativeBundlePath || true)
    for ((i = 0; i < count; i++)); do
        for key in BundleIsRelocatable BundleIsVersionChecked; do
            /usr/libexec/PlistBuddy -c "Add :${i}:${key} bool false" "$plist" 2>/dev/null \
                || /usr/libexec/PlistBuddy -c "Set :${i}:${key} false" "$plist"
        done
    done

    pkgbuild --root "$root" \
             --component-plist "$plist" \
             --identifier "$identifier" \
             --version "$VERSION" \
             --install-location "$location" \
             "${PKGS}/occultation-${name}.pkg" >/dev/null
    echo "    ${name}: ${location}"
}

echo "==> Building component packages"
build_component vst3 "${STAGE}/vst3" "${IDENTIFIER_BASE}.vst3" "/Library/Audio/Plug-Ins/VST3"
build_component au   "${STAGE}/au"   "${IDENTIFIER_BASE}.au"   "/Library/Audio/Plug-Ins/Components"
build_component app  "${STAGE}/app"  "${IDENTIFIER_BASE}.app"  "/Applications"

# --- Installer UI resources ---------------------------------------------
cp "${SCRIPT_DIR}/resources/welcome.html" "${SCRIPT_DIR}/resources/conclusion.html" "$RESOURCES/"
# productbuild wants the licence inside --resources; keep the repo's LICENSE as
# the single source of truth rather than a copy that can drift.
cp "${REPO_ROOT}/LICENSE" "${RESOURCES}/LICENSE.txt"

DISTRIBUTION="${WORK}/distribution.xml"
sed -e "s/@VERSION@/${VERSION}/g" \
    -e "s/@MIN_OS@/${MIN_OS}/g" \
    -e "s/@HOST_ARCHS@/${HOST_ARCHS}/g" \
    "${SCRIPT_DIR}/distribution.xml.in" > "$DISTRIBUTION"

# --- Combine into the product archive -----------------------------------
echo "==> Building installer"
mkdir -p "$(dirname "$OUTPUT")"
productbuild_args=(
    --distribution "$DISTRIBUTION"
    --package-path "$PKGS"
    --resources "$RESOURCES"
)
[[ -n "$SIGN_PKG" ]] && productbuild_args+=(--sign "$SIGN_PKG" --timestamp)
productbuild "${productbuild_args[@]}" "$OUTPUT" >/dev/null

# --- Optional notarisation ----------------------------------------------
# Needs a stored profile:
#   xcrun notarytool store-credentials <profile> --apple-id … --team-id … --password <app-specific>
if [[ -n "$NOTARIZE_PROFILE" ]]; then
    if [[ -z "$SIGN_PKG" || -z "$SIGN_APP" ]]; then
        echo "error: --notarize requires both --sign-app and --sign-pkg (Apple rejects unsigned submissions)" >&2
        exit 1
    fi
    echo "==> Notarising (this waits on Apple, typically a few minutes)"
    xcrun notarytool submit "$OUTPUT" --keychain-profile "$NOTARIZE_PROFILE" --wait
    xcrun stapler staple "$OUTPUT"
    spctl --assess --type install --verbose=2 "$OUTPUT" || true
fi

echo "==> Verifying"
pkgutil --check-signature "$OUTPUT" 2>&1 | sed 's/^/    /' || true
installer -pkg "$OUTPUT" -pkginfo >/dev/null && echo "    payload readable by installer(8)"

echo
echo "Installer: ${OUTPUT}"
echo "Size:      $(du -h "$OUTPUT" | cut -f1)"
