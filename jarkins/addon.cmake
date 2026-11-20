# Jarkins' add-on for Tekken 3 Expanded.
#
# Read by the Expanded's add-on hook (TEKKEN3_ADDON_API 1, addons/README.md):
# the game's CMakeLists.txt globs addons/*/addon.cmake, includes each one in a
# scope of its own (TEKKEN3_ADDON_DIR, TEKKEN3_ADDON_ID set) and then builds
# psx-runtime with what they added. Nothing of the game is edited: delete this
# folder, run Update & play again, and the game is back as it was.
#
# What this file does, in the order the API expects it:
#   1. links this add-on's C sources into the game (they are what the mods run),
#   2. stages the package manifests beside the game (they are what the launcher's
#      Mods page lists), and any data the mod reads at run time,
#   3. optionally names a Python setup step (TEKKEN3_ADDON_SETUP, see README.md).
#
# The strip is drawn by a GPU packet of the add-on's own, spliced into the frame's
# ordering table (1, below): it needs none of the game's texture space and no
# other add-on's overlay.

if(NOT DEFINED TEKKEN3_ADDON_API)
    message(WARNING "Jarkins: this Tekken 3 Expanded has no add-on hook (it needs 1.5.0 or later); the add-on is not built in")
    return()
endif()

set(JARKINS_VERSION "0.2.0")

# 1. C sources linked into psx-runtime: the plugins the manifests select.
set(JARKINS_SOURCES
    src/jarkins_osd.c
    src/jarkins_video.c
    src/jarkins_plates.c
    src/jarkins_input.c
    src/jarkins_damage.c
)
set(_sources "")
foreach(_s IN LISTS JARKINS_SOURCES)
    list(APPEND _sources "${TEKKEN3_ADDON_DIR}/${_s}")
endforeach()
target_sources(psx-runtime PRIVATE ${_sources})
# The frame's ordering table is the runtime's "the GPU is about to draw this
# frame": the add-on wraps it to put its own packet in.
#
# Another add-on may wrap the same routine - TMST's overlay does - and two
# definitions of __wrap_<sym> do not link at all: with both folders in, the
# build failed and the Expanded left BOTH add-ons out of the game. So when
# TMST's overlay is in this build, its wrapper is compiled under a name of ours
# and called from ours (JARKINS_CHAINS_TMST): both overlays draw, and either
# folder still builds and runs on its own. TMST's folder is not edited, and this
# add-on does not need TMST - this only matters when TMST is already there.
tekken3_wrap_symbol(gpu_ws_prepass_linked_list WRAPPER src/jarkins_video.c)
set(JARKINS_CHAINS_TMST OFF)
set(_jarkins_tmst_overlay "${CMAKE_SOURCE_DIR}/addons/tmst/src/core/tmst_overlay.c")
list(FIND TEKKEN3_ADDONS_SKIP tmst _jarkins_tmst_skipped)
if(EXISTS "${_jarkins_tmst_overlay}" AND _jarkins_tmst_skipped EQUAL -1)
    file(STRINGS "${_jarkins_tmst_overlay}" _jarkins_tmst_wrap
         REGEX "^void[ \t]+__wrap_gpu_ws_prepass_linked_list" LIMIT_COUNT 1)
    if(_jarkins_tmst_wrap)
        set(JARKINS_CHAINS_TMST ON)
        # Deferred: TMST sets its own definitions on this file when its addon.cmake
        # runs, and this must land after that, not be replaced by it. A deferred
        # call reads its arguments in the directory's scope, so the path is built
        # from CMAKE_SOURCE_DIR, not from a variable of this included file.
        cmake_language(DEFER CALL set_property
            SOURCE "${CMAKE_SOURCE_DIR}/addons/tmst/src/core/tmst_overlay.c"
            APPEND PROPERTY COMPILE_DEFINITIONS
            "__wrap_gpu_ws_prepass_linked_list=jarkins_tmst_prepass")
        message(STATUS "Jarkins: TMST's overlay chains through this add-on")
    endif()
endif()

set(_jarkins_defs "JARKINS_VERSION=\"${JARKINS_VERSION}\"")
if(JARKINS_CHAINS_TMST)
    list(APPEND _jarkins_defs JARKINS_CHAINS_TMST)
endif()
set_source_files_properties(${_sources} PROPERTIES
    COMPILE_DEFINITIONS "${_jarkins_defs}")

# 2. Packages: addons/jarkins/mods/<package id>/<version>/manifest.toml, put
#    beside the game as mods/packages/<package id>/ (what the Mods page reads).
tekken3_addon_stage(FROM "${TEKKEN3_ADDON_DIR}/mods/jarkins.lab" TO "mods/packages/jarkins.lab")
# The 6 Costume Slots Mod: switch it on in the Mods page and each fighter's
# name plate takes the colour of the costume slot he came in.
tekken3_addon_stage(FROM "${TEKKEN3_ADDON_DIR}/mods/jarkins.plates" TO "mods/packages/jarkins.plates")


# 3. A setup step (needed only when the mod is made from the player's own files:
#    discs, ROM zips). Uncomment and write the script:
# set(TEKKEN3_ADDON_SETUP setup/jarkins_setup.py)
# It runs before every build, from this folder, as:
#     <the Expanded's python> -u setup/jarkins_setup.py --no-build
# It must ask nothing (no window, no input) and exit 0. Keep what it makes in
# this folder (workspace/) or stage it above.

message(STATUS "Jarkins ${JARKINS_VERSION} (add-on hook ${TEKKEN3_ADDON_API}): ${TEKKEN3_ADDON_ID}")
#
# The files this add-on ships, hashed together. The Expanded rebuilds when the digest
# of this file changes, so this line is what makes a new copy of the add-on build:
# without it, a fixed source file would leave the old compiled code in place.
# jarkins-release-fingerprint: cd38580d1426aae3012e775a57eb9d1c031f2a1499dbd9724c6a852e6a40f46f
