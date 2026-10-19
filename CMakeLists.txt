cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n"
        "This project expects the psxrecomp framework checkout at ${PSXRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive\n"
        "If psxrecomp/ is not a submodule, set -DPSXRECOMP_ROOT=<path-to-psxrecomp>.")
endif()

project(MsPacManMazeMadnessRecomp LANGUAGES C CXX)

# --- Opt-in product surfaces (OFF by default in the framework) -------------
# Scaffold prompts / flags fill these. Without recomp-ui, PSX_RECOMP_UI is OFF.
# The wizard (WIZARD_CMAKE_BLOCK / ENABLE_SETUP_WIZARD) is the first-run disc
# picker; releases are compiled from the committed generated/ C, so it never
# needs to generate or rebuild anything on a player's machine.
set(PSX_RECOMP_UI OFF CACHE BOOL
    "No recomp-ui submodule in this scaffold" FORCE)
# Full netplay UI — enable after adding recomp-ui + testing:
# if(EXISTS "${PSXRECOMP_ROOT}/lib/recomp-net/CMakeLists.txt")
#     set(PSX_NETPLAY ON CACHE BOOL "…" FORCE)
# endif()
# First-run setup wizard — enable after testing:
# set(PSX_SETUP_WIZARD ON CACHE BOOL "…" FORCE)
# Horizon has no dynamic loader (no libdl); overlays are linked statically.
if(NINTENDO_SWITCH)
    set(CMAKE_DL_LIBS "")
    # devkitPro's Switch.cmake makes Release "-g -O2". Full speed on the
    # console was measured with -O3, as on the other platforms.
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# This project's own mod catalog. The FRAMEWORK stages it -- together with its
# builtin psx.* packages -- into <exe-dir>/mods/bundled, and refuses to
# configure if a catalog exists here but is not declared. Never copy it by
# hand: five titles did, and framework 4cc04be3's rename of the staged
# directory broke all five silently (bead beads-eio.3.101).
set(PSX_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(NOT IS_DIRECTORY "${PSX_PRELOADED_MODS}")
    set(PSX_PRELOADED_MODS "NONE")
endif()
# The Switch defaults to the original 4:3 picture: in 16:9 the extra
# bookkeeping costs ~12% of a frame, and full speed needs it. Stage a copy of
# the catalog with the widescreen features off by default; mods/state.toml on
# the SD card can still turn them on.
if(NINTENDO_SWITCH AND NOT PSX_PRELOADED_MODS STREQUAL "NONE")
    set(_mpmm_ws_manifest
        "packages/mpmm.enhancement.widescreen/1.0.0/manifest.toml")
    set(_mpmm_switch_mods "${CMAKE_CURRENT_BINARY_DIR}/mods-preloaded-switch")
    file(REMOVE_RECURSE "${_mpmm_switch_mods}")
    file(COPY "${PSX_PRELOADED_MODS}/" DESTINATION "${_mpmm_switch_mods}")
    file(READ "${PSX_PRELOADED_MODS}/${_mpmm_ws_manifest}" _mpmm_ws)
    string(REPLACE "default_enabled = true" "default_enabled = false"
        _mpmm_ws "${_mpmm_ws}")
    file(WRITE "${_mpmm_switch_mods}/${_mpmm_ws_manifest}" "${_mpmm_ws}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PSX_PRELOADED_MODS}/${_mpmm_ws_manifest}")
    set(PSX_PRELOADED_MODS "${_mpmm_switch_mods}")
endif()

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    # ENABLE_NETPLAY_IF_PRESENT
    # NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    # ENABLE_SETUP_WIZARD
    WINDOW_TITLE "MsPacManMazeMadness Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    # AOT overlays (MAIN.EXE + CLASSIC.EXE) from aot/overlays.json. Keep it
    # above GEN_FULL_GLOB: a list argument would swallow it as another source.
    GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c"
    MAX_PLAYERS 2
    # LAUNCHER_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    # Committed game C (psxrecomp_cli.py generate; git add generated). Release
    # CI configures with -DPSXRECOMP_REQUIRE_GAME_C=ON so a checkout without
    # it fails instead of building a setup host.
    GEN_MARKER "generated/SLUS_010.18_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_010.18_full_*.c"
    # CODEGEN_SETUP_SOURCES — add with recomp-ui / wizard later
)

# Game-owned trusted mod plugins (selected by mods/preloaded/packages manifests).
target_sources(psx-runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/mpmm_widescreen.c")

# Nintendo Switch (devkitPro's Switch.cmake toolchain): Horizon platform layer
# and the .nro. See switch/switch_platform.c for what the wraps are for.
if(NINTENDO_SWITCH)
    target_sources(psx-runtime PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/switch/switch_platform.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/switch/switch_terminate.cpp")
    target_link_options(psx-runtime PRIVATE
        "-Wl,--wrap=main" "-Wl,--wrap=rename" "-Wl,--wrap=getcwd"
        "-Wl,--wrap=pthread_create" "-Wl,--wrap=abort")
    # Direct GL calls resolve through libGLESv2's exports (Mesa dispatch).
    target_link_libraries(psx-runtime PRIVATE GLESv2 glapi drm_nouveau nx)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _MPMM_VERSION)
    string(STRIP "${_MPMM_VERSION}" _MPMM_VERSION)
    nx_generate_nacp(OUTPUT mspacman.nacp
        NAME "Ms. Pac-Man Maze Madness Recompiled"
        AUTHOR "adumah"
        VERSION "${_MPMM_VERSION}")
    # Homebrew Menu icon: switch/icon.jpg if you add one (256x256 JPEG, never
    # committed); otherwise Ms. Pac-Man from the classic-mode cabinet art on
    # your disc (tools/make_switch_icon.py, needs Pillow); otherwise libnx's
    # default icon.
    set(_mpmm_icon "${CMAKE_CURRENT_SOURCE_DIR}/switch/icon.jpg")
    if(NOT EXISTS "${_mpmm_icon}")
        set(_mpmm_icon "${CMAKE_CURRENT_BINARY_DIR}/icon-from-disc.jpg")
        set(_mpmm_cue "${CMAKE_CURRENT_SOURCE_DIR}/disc/Ms. Pac-Man - Maze Madness.cue")
        find_program(MPMM_PYTHON3 python3 REQUIRED)
        file(REMOVE "${_mpmm_icon}")
        if(EXISTS "${_mpmm_cue}")
            execute_process(
                COMMAND "${MPMM_PYTHON3}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_switch_icon.py"
                    --cue "${_mpmm_cue}" --out "${_mpmm_icon}"
                OUTPUT_QUIET)
        endif()
    endif()
    if(EXISTS "${_mpmm_icon}")
        message(STATUS "Switch icon: ${_mpmm_icon}")
        set(_mpmm_icon_args ICON "${_mpmm_icon}")
    else()
        message(STATUS "Switch icon: libnx default")
        set(_mpmm_icon_args "")
    endif()
    nx_create_nro(psx-runtime
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mspacman.nro"
        NACP mspacman.nacp
        ${_mpmm_icon_args})
endif()

# Stage runtime configs next to the exe for direct build/ launches. Only copy
# files that exist — never fail POST_BUILD when game_options.toml is absent.
foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()
