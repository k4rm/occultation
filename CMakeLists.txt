cmake_minimum_required(VERSION 3.22)

project(Occultation VERSION 1.0.0 LANGUAGES C CXX)

# Default to Release. This is a single-config generator (Makefiles/Ninja), so
# an unset CMAKE_BUILD_TYPE means NO optimisation flags at all — and the
# README's own build commands don't pass one. For a plugin doing per-frame
# image processing and per-sample DSP on a real-time audio thread, silently
# shipping an -O0 build is the difference between working and dropping out.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS floor. Every Apple Silicon Mac ever sold shipped with macOS 11 or
# later, so this covers the whole arm64 install base rather than only machines
# on the newest OS.
#
# This USED to be deliberately unset, because the floor was dictated by
# dependencies rather than by us: OpenCV and FFmpeg arrived as Homebrew bottles
# built for the build machine's own macOS (minos 26.0), dylibbundler shipped
# those dylibs inside every bundle, and dyld enforces each image's
# LC_BUILD_VERSION at load time — so claiming a lower target here produced a
# plugin that advertised macOS 11 and still died in dyld.
#
# Both dependencies are now built from source against this same target instead
# (see the OpenCV section below and the README's "The macOS floor"), so nothing
# in the bundle imposes a floor we didn't choose. Section 2a still verifies
# that against the real binaries rather than trusting this line.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# 1. Fetch JUCE (adjust GIT_TAG to your preferred version or use a local path via add_subdirectory)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.1
)
FetchContent_MakeAvailable(JUCE)

# 2. Locate OpenCV — preferring a lean, STATIC build over Homebrew's bottle.
#
# Homebrew's opencv links a parallel OpenBLAS, which links libomp (LLVM's
# OpenMP runtime). dylibbundler then copies libomp into EVERY plugin bundle,
# so a host that loads more than one of our formats at once (Ableton Live
# scanning the VST3 and the AU together) ends up with two libomp images at
# two different paths. LLVM's OpenMP deliberately calls abort() on duplicate
# registration, taking the whole host process down with it — reproduced as
# Live dying mid-scan, with __kmp_register_library_startup -> __kmp_fatal
# right in the crash log.
#
# A static OpenCV built with WITH_OPENMP=OFF/WITH_LAPACK=OFF (it then uses
# GCD for threading on macOS) has no libomp anywhere in the graph, so the
# duplicate simply can't happen. It also drops OpenBLAS, gfortran, quadmath
# and the whole VTK pile out of the bundles. See README for the build recipe.
set(OCCULTATION_OPENCV_ROOT "$ENV{HOME}/Documents/dev/opencv-lean/install"
    CACHE PATH "Root of a lean static OpenCV install (see README); falls back to the system OpenCV if absent")
if(EXISTS "${OCCULTATION_OPENCV_ROOT}/lib/cmake/opencv5")
    set(OpenCV_DIR "${OCCULTATION_OPENCV_ROOT}/lib/cmake/opencv5")
    message(STATUS "Occultation: using lean static OpenCV at ${OCCULTATION_OPENCV_ROOT}")
else()
    message(WARNING
        "Occultation: lean static OpenCV not found at ${OCCULTATION_OPENCV_ROOT} - falling back to the "
        "system OpenCV. If that one links OpenBLAS/libomp, loading two plugin formats in one host "
        "(e.g. Ableton scanning both VST3 and AU) will abort the host. See README.")
endif()
# 2a-pre. The lean FFmpeg this project builds from source (LGPL 2.1, decode
# only, no external codec libraries, built for CMAKE_OSX_DEPLOYMENT_TARGET).
# OpenCV's videoio links against it for rtsp/rtmp/http(s) capture; local files
# and cameras go through AVFoundation instead. PKG_CONFIG_LIBDIR (not
# PKG_CONFIG_PATH) is what keeps OpenCV's own detection off a Homebrew ffmpeg —
# see the README's rebuild recipe.
set(OCCULTATION_FFMPEG_ROOT "$ENV{HOME}/Documents/dev/ffmpeg-lean/install"
    CACHE PATH "Root of the lean LGPL FFmpeg build (see README)")

find_package(OpenCV REQUIRED)

# 2a. Report the macOS floor the *dependencies* impose, and check any
# CMAKE_OSX_DEPLOYMENT_TARGET / CMAKE_OSX_ARCHITECTURES against it.
#
# Neither of those is set by this project on purpose: widening Occultation's own
# reach past its dependencies' buys nothing. OpenCV and FFmpeg arrive as
# Homebrew bottles (or a local OpenCV built from them), single-arch and compiled
# for whatever macOS the build machine runs, and dylibbundler ships those dylibs
# inside every bundle — where dyld then enforces each image's own LC_BUILD_VERSION
# at load time. Lower this project's target alone and you get a plugin that
# claims to run on macOS 11 and dies in dyld; add x86_64 alone and it fails at
# link. Both really need OpenCV and FFmpeg rebuilt first (see README), so surface
# the mismatch here instead of letting it look like a bug later.
if(APPLE)
    set(_dep_names "")
    set(_dep_paths "")
    if(EXISTS "${OCCULTATION_OPENCV_ROOT}/lib/libopencv_core.a")
        list(APPEND _dep_names "OpenCV")
        list(APPEND _dep_paths "${OCCULTATION_OPENCV_ROOT}/lib/libopencv_core.a")
    endif()
    # FFmpeg, from the lean LGPL build this project ships (see
    # OCCULTATION_FFMPEG_ROOT above) — deliberately NOT `brew --prefix ffmpeg`.
    # Homebrew's bottle is built for the build machine's own macOS and would
    # report a floor that has nothing to do with what actually goes in the
    # bundle; these five dylibs are the ones dylibbundler copies, so these are
    # the ones whose LC_BUILD_VERSION dyld will enforce on a user's machine.
    if(EXISTS "${OCCULTATION_FFMPEG_ROOT}/lib/libavcodec.dylib")
        list(APPEND _dep_names "FFmpeg")
        list(APPEND _dep_paths "${OCCULTATION_FFMPEG_ROOT}/lib/libavcodec.dylib")
    endif()

    set(_dep_min_os "")
    set(_dep_min_os_source "")
    set(_dep_archs "")
    set(_dep_archs_seen FALSE)
    set(_dep_index 0)
    foreach(_path IN LISTS _dep_paths)
        list(GET _dep_names ${_dep_index} _name)
        math(EXPR _dep_index "${_dep_index} + 1")

        execute_process(COMMAND otool -l "${_path}"
                        OUTPUT_VARIABLE _otool OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        # First minos in the dump; for a static archive that's its first member,
        # which is representative - the whole thing was built in one pass.
        if(_otool MATCHES "minos ([0-9.]+)")
            set(_minos "${CMAKE_MATCH_1}")
            if(NOT _dep_min_os OR _minos VERSION_GREATER _dep_min_os)
                set(_dep_min_os "${_minos}")
                set(_dep_min_os_source "${_name}")
            endif()
        endif()

        execute_process(COMMAND lipo -archs "${_path}"
                        OUTPUT_VARIABLE _archs OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REPLACE " " ";" _archs "${_archs}")
        if(NOT _dep_archs_seen)
            set(_dep_archs ${_archs})
            set(_dep_archs_seen TRUE)
        else()
            # The usable set is the intersection: an arch missing from any one
            # dependency can't be built at all.
            set(_kept "")
            foreach(_a IN LISTS _dep_archs)
                if(_a IN_LIST _archs)
                    list(APPEND _kept "${_a}")
                endif()
            endforeach()
            set(_dep_archs ${_kept})
        endif()
    endforeach()

    if(_dep_min_os)
        string(REPLACE ";" ", " _dep_archs_pretty "${_dep_archs}")
        message(STATUS "Occultation: dependencies allow macOS ${_dep_min_os}+ on ${_dep_archs_pretty} "
                       "(floor from ${_dep_min_os_source})")

        if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS _dep_min_os)
            message(WARNING
                "Occultation: CMAKE_OSX_DEPLOYMENT_TARGET is ${CMAKE_OSX_DEPLOYMENT_TARGET}, but "
                "${_dep_min_os_source} was built for macOS ${_dep_min_os}. The bundles will claim "
                "${CMAKE_OSX_DEPLOYMENT_TARGET} and still fail to load below ${_dep_min_os}, because dyld "
                "checks each bundled dylib too. Rebuild OpenCV and FFmpeg against "
                "${CMAKE_OSX_DEPLOYMENT_TARGET} first - see the README's packaging section.")
        endif()

        foreach(_arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            if(_dep_archs AND NOT _arch IN_LIST _dep_archs)
                message(WARNING
                    "Occultation: CMAKE_OSX_ARCHITECTURES asks for ${_arch}, which the dependencies "
                    "don't provide (they are ${_dep_archs_pretty}). The link will fail. Build universal "
                    "OpenCV/FFmpeg first - see the README's packaging section.")
            endif()
        endforeach()
    endif()
endif()

# 2b. Embed the celestial-body images (Occulting Object overlay) as
# compiled-in binary data — juce_add_binary_data generates a small static
# library exposing each file as BinaryData::<name>_png/_pngSize, so these
# ship inside the plugin binary itself rather than needing to be found
# alongside it at runtime (which would break once the .app/.vst3 bundle is
# moved or copied to another machine).
juce_add_binary_data(OccultationAssets
    SOURCES
        Resources/celestial/sun.png
        Resources/celestial/moon_phases/moon_phase_1.png
        Resources/celestial/moon_phases/moon_phase_2.png
        Resources/celestial/moon_phases/moon_phase_3.png
        Resources/celestial/moon_phases/moon_phase_4.png
        Resources/celestial/moon_phases/moon_phase_5.png
        Resources/celestial/moon_phases/moon_phase_6.png
        Resources/celestial/moon_phases/moon_phase_7.png
        Resources/celestial/moon_phases/moon_phase_8.png
        Resources/celestial/mercury.png
        Resources/celestial/venus.png
        Resources/celestial/earth.png
        Resources/celestial/mars.png
        Resources/celestial/jupiter.png
        Resources/celestial/saturn.png
        Resources/celestial/uranus.png
        Resources/celestial/neptune.png
        Resources/celestial/pluto.png
)

# 3. Define the JUCE VST3 instrument plugin target — was a pure MIDI-effect
# plugin; now an instrument (real audio via SynthEngine) that also still
# produces MIDI output (NEEDS_MIDI_OUTPUT), a combination both the VST3 spec
# and Ableton Live actually support (unlike bus-less MIDI-effect VST3s,
# which Ableton's loader rejects outright — see PluginProcessor.cpp's
# constructor comment and git history for that investigation).
juce_add_plugin(OccultationPlugin
    COMPANY_NAME "Apokrypsi"
    PLUGIN_NAME "Occultation"
    IS_MIDI_EFFECT FALSE
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "Occultation"
    BUNDLE_ID "com.apokrypsi.occultation"
    # AU (unlike VST3, which can fall back to a generated GUID) requires
    # these two 4-char codes — added now that AU is a built format, so
    # Logic/GarageBand (VST3-incapable, unlike Ableton) can load it too.
    PLUGIN_MANUFACTURER_CODE Apok
    PLUGIN_CODE Occl
    # Bundle icon (Standalone .app, and the VST3/AU bundles). BIG and SMALL
    # must be genuinely different sizes: pointing both at the same 1024px
    # source made juceaide emit the same 1024 ('ic10') entry into the .icns
    # twice, which iconutil refuses to parse and the Dock renders as a blank
    # white square. A 1024 + 256 pair gives it distinct entries to work with.
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Resources/occultation-wizard-icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/occultation-wizard-icon-256.png"
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "We use the camera for real-time MIDI generation and tracking."
    JUCE_STANDALONE_CUSTOM_START_KEY 1
)



# 4. Add your source and header files
target_sources(OccultationPlugin
    PRIVATE
        Sources/PluginProcessor.cpp
        Sources/PluginProcessor.h
        Sources/PluginEditor.cpp
        Sources/PluginEditor.h
        Sources/SkyMapRenderer.cpp
        Sources/SkyMapRenderer.h
        Sources/SynthEngine.cpp
        Sources/SynthEngine.h
)

# 5. Link required JUCE modules and OpenCV libraries
target_link_libraries(OccultationPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_video
        OccultationAssets
        ${OpenCV_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(OccultationPlugin
    PRIVATE
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_definitions(OccultationPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_CAMERA=1
)

# 6. Provide OpenCV include directories
target_include_directories(OccultationPlugin
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
)

# --- Bundle OpenCV/FFmpeg runtime dylibs, macOS ---
# Without this, the VST3/Standalone binaries keep pointing at Homebrew's
# /opt/homebrew/opt/opencv/... and /opt/homebrew/opt/ffmpeg/... dylibs, so they
# only run on a machine that already has `brew install opencv` done — not
# something to ask of anyone just trying to use the plugin. dylibbundler
# copies every non-system dependency (OpenCV's libs, and FFmpeg's underneath
# those) into each bundle's own Contents/Frameworks and rewrites the load
# commands to @executable_path/../Frameworks, so the bundle is self-contained.
if(APPLE)
    find_program(DYLIBBUNDLER_EXECUTABLE dylibbundler)
    if(DYLIBBUNDLER_EXECUTABLE)
        function(occultation_bundle_dylibs target)
            add_custom_command(
                TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Bundling OpenCV/FFmpeg runtime dylibs into ${target}..."
                COMMAND ${DYLIBBUNDLER_EXECUTABLE}
                        -od -b
                        -x "$<TARGET_FILE:${target}>"
                        -d "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks/"
                        -p "@loader_path/../Frameworks/"
                # @executable_path resolves against the *loading process's*
                # binary, not this bundle's own — fine for the Standalone .app
                # (which loads itself), but wrong for the VST3, where the
                # loading process is the host (Ableton Live, etc.): it would
                # look for dylibs next to Live's own executable and fail to
                # load the plugin at all. @loader_path always resolves against
                # whichever binary actually holds the load command, host or not.
                #
                # dylibbundler can also leave a dylib (OpenEXR, reached via
                # several OpenCV modules at once) with the same rpath entry
                # added more than once, which dyld then refuses to load at all
                # — collapse every binary back down to exactly one clean entry.
                COMMAND bash "${CMAKE_SOURCE_DIR}/cmake/dedupe_rpaths.sh"
                        "$<TARGET_BUNDLE_DIR:${target}>/Contents/MacOS"
                        "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
                # Bundling rewrites the binary's load commands, which invalidates
                # whatever ad-hoc signature the linker applied automatically —
                # re-sign or the OS refuses to run it.
                COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                COMMENT "Bundling runtime dylibs into ${target} for a self-contained build"
            )
        endfunction()

        occultation_bundle_dylibs(OccultationPlugin_VST3)
        occultation_bundle_dylibs(OccultationPlugin_Standalone)
        occultation_bundle_dylibs(OccultationPlugin_AU)
    else()
        message(WARNING
            "dylibbundler not found - OccultationPlugin_VST3/_Standalone/_AU will only run on "
            "machines with OpenCV (and FFmpeg) already installed via Homebrew. Install with "
            "`brew install dylibbundler` and reconfigure for a self-contained, redistributable build.")
    endif()
endif()

# --- Post-Build Copy and Signing Step for macOS ---
if(APPLE)
    set(USER_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")

    add_custom_command(
        TARGET OccultationPlugin_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_VST3_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Copying VST3 bundle to user library..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${USER_VST3_DIR}/Occultation.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:OccultationPlugin_VST3>" "${USER_VST3_DIR}/Occultation.vst3"
        COMMAND ${CMAKE_COMMAND} -E echo "Clearing attributes and signing VST3 bundle..."
        COMMAND xattr -cr "${USER_VST3_DIR}/Occultation.vst3"
        COMMAND codesign --force --deep --sign - "${USER_VST3_DIR}/Occultation.vst3"
        COMMENT "Deploying, clearing quarantine attributes, and ad-hoc signing VST3 bundle for Ableton Live"
    )

    # AU components live in a different directory than VST3 (~/Library/Audio/
    # Plug-Ins/Components, not .../VST3) and are what Logic/GarageBand — which
    # can't load VST3 at all — actually scan, so it needs the exact same
    # deploy-and-sign treatment as the VST3 step just above.
    set(USER_AU_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")

    add_custom_command(
        TARGET OccultationPlugin_AU POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_AU_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Copying AU component to user library..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${USER_AU_DIR}/Occultation.component"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:OccultationPlugin_AU>" "${USER_AU_DIR}/Occultation.component"
        COMMAND ${CMAKE_COMMAND} -E echo "Clearing attributes and signing AU component..."
        COMMAND xattr -cr "${USER_AU_DIR}/Occultation.component"
        COMMAND codesign --force --deep --sign - "${USER_AU_DIR}/Occultation.component"
        COMMENT "Deploying, clearing quarantine attributes, and ad-hoc signing AU component for Logic/GarageBand"
    )
endif()

# --- Distributable .pkg installer, macOS ---
# The post-build steps above deploy into the *developer's* own ~/Library, which
# is exactly wrong for anyone else: they'd have to be told where to drag three
# bundles. `cmake --build build --target installer` instead wraps the built
# bundles into one double-clickable product archive (pkgbuild per format,
# combined by productbuild) that installs system-wide with a format picker.
#
# Deliberately not part of ALL: it re-signs and re-stages ~190MB of bundles and,
# with --notarize, blocks on Apple's servers — not something every incremental
# build should pay for. See packaging/build_installer.sh for signing/notarising.
if(APPLE)
    set(OCCULTATION_SIGN_APP "" CACHE STRING
        "Developer ID Application identity used to sign the bundles for the installer (empty: keep the ad-hoc signature)")
    set(OCCULTATION_SIGN_PKG "" CACHE STRING
        "Developer ID Installer identity used to sign the .pkg (empty: unsigned)")
    set(OCCULTATION_NOTARIZE_PROFILE "" CACHE STRING
        "notarytool keychain profile; set to notarise and staple the .pkg (requires both signing identities)")

    set(_installer_args "")
    if(OCCULTATION_SIGN_APP)
        list(APPEND _installer_args --sign-app "${OCCULTATION_SIGN_APP}")
    endif()
    if(OCCULTATION_SIGN_PKG)
        list(APPEND _installer_args --sign-pkg "${OCCULTATION_SIGN_PKG}")
    endif()
    if(OCCULTATION_NOTARIZE_PROFILE)
        list(APPEND _installer_args --notarize "${OCCULTATION_NOTARIZE_PROFILE}")
    endif()

    add_custom_target(installer
        COMMAND "${CMAKE_SOURCE_DIR}/packaging/build_installer.sh"
                --build-dir "${CMAKE_BINARY_DIR}"
                --config "$<CONFIG>"
                --version "${PROJECT_VERSION}"
                ${_installer_args}
        DEPENDS OccultationPlugin_VST3 OccultationPlugin_AU OccultationPlugin_Standalone
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
        COMMENT "Building the Occultation .pkg installer"
    )
endif()