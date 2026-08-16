# openQ4 Android build.
#
# Meson stays the build system for every desktop platform; this file exists
# only because the Android NDK/Gradle toolchain speaks CMake. It deliberately
# mirrors the desktop meson.build target-for-target rather than inventing a new
# layout, and it reuses the same source-list scripts under tools/build so the
# two builds can never drift:
#
#   libquake4.so                  the engine client, module-only renderer shape
#   librenderer-gles_arm64.so     the ES 3.0 renderer module
#   libgame-sp_arm64.so           the single-player game module
#
# The renderer and game modules are real dlopen()ed modules here, exactly as on
# Linux, so each keeps its own idlib copy and the ODR hazards the desktop build
# is careful about stay solved the same way.

cmake_minimum_required(VERSION 3.22)

if(NOT ANDROID)
    message(FATAL_ERROR "openQ4's CMake build is Android-only. Every other platform builds with Meson; see BUILDING.md.")
endif()

project(openQ4 C CXX)

set(OQ4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(OQ4_SRC "${OQ4_ROOT}/src")
set(OQ4_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OQ4_GEN}")

# Game-library sources. openQ4 consumes them straight out of the companion
# repository (see AGENTS.md); the wrapper module checks it out next to us.
if(NOT DEFINED OPENQ4_GAMELIBS_DIR)
    set(OPENQ4_GAMELIBS_DIR "${OQ4_ROOT}/../openQ4-game")
endif()
get_filename_component(OPENQ4_GAMELIBS_DIR "${OPENQ4_GAMELIBS_DIR}" ABSOLUTE)
if(NOT IS_DIRECTORY "${OPENQ4_GAMELIBS_DIR}/src/game")
    message(FATAL_ERROR "openQ4-game sources not found at ${OPENQ4_GAMELIBS_DIR}/src/game")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---------------------------------------------------------------------------
# Source lists, from the same scripts Meson uses
# ---------------------------------------------------------------------------

function(oq4_run_script OUT_VAR)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${ARGN}
        WORKING_DIRECTORY "${OQ4_ROOT}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        RESULT_VARIABLE _result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "openQ4 build script failed (${ARGN}):\n${_stderr}")
    endif()
    set(${OUT_VAR} "${_stdout}" PARENT_SCOPE)
endfunction()

function(oq4_source_list OUT_VAR ROOT)
    oq4_run_script(_raw ${ARGN})
    string(REPLACE "\n" ";" _lines "${_raw}")
    set(_paths "")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _line)
        if(_line)
            list(APPEND _paths "${ROOT}/${_line}")
        endif()
    endforeach()
    set(${OUT_VAR} "${_paths}" PARENT_SCOPE)
endfunction()

set(OQ4_SOURCES_PY "${OQ4_ROOT}/tools/build/meson_sources.py")
set(OQ4_LIST_SOURCES_PY "${OQ4_ROOT}/tools/build/list_sources.py")

# The client sheds the renderer sources (--renderer module) because there is no
# desktop GL on Android at all: the ES module is the only renderer that exists.
oq4_source_list(OQ4_ENGINE_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}"
    --host-system android --platform-backend sdl3 --include-game false --renderer module)
oq4_source_list(OQ4_IMAGETOOLS_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}" --emit imagetools)
oq4_source_list(OQ4_RENDER_GEO_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}" --emit render_geo)
oq4_source_list(OQ4_RENDERER_GLES_SOURCES "${OQ4_ROOT}" "${OQ4_SOURCES_PY}" --emit renderer_gles)
oq4_source_list(OQ4_BSE_SOURCES "${OQ4_ROOT}" "${OQ4_LIST_SOURCES_PY}" "${OQ4_ROOT}" src/bse)
oq4_source_list(OQ4_IDLIB_SOURCES "${OQ4_ROOT}" "${OQ4_LIST_SOURCES_PY}" "${OQ4_ROOT}"
    src/idlib src/idlib/math/Simd_AltiVec.cpp)
oq4_source_list(OQ4_GAME_SP_SOURCES "${OPENQ4_GAMELIBS_DIR}" "${OQ4_LIST_SOURCES_PY}"
    "${OPENQ4_GAMELIBS_DIR}" src/game src/game/Callbacks.cpp src/game/gamesys/Callbacks.cpp)

# ---------------------------------------------------------------------------
# Generated headers
# ---------------------------------------------------------------------------

oq4_run_script(_version_out "${OQ4_ROOT}/tools/build/openq4_version.py"
    --source-root "${OQ4_ROOT}"
    --track dev
    --base-version 0.1.010
    --header-out "${OQ4_GEN}/openq4_version_generated.h")

oq4_run_script(_savegame_out "${OQ4_ROOT}/tools/build/generate_savegame_compat_header.py"
    --project-root "${OQ4_ROOT}"
    --game-stage-root "${OPENQ4_GAMELIBS_DIR}"
    --header-out "${OQ4_GEN}/openq4_savegame_compat_generated.h")

# openq4_paks_generated.h carries the MD5s the engine fatal-errors on if the
# shipped baseoq4/pak0.pk4 and pak1.pk4 do not match, so the Android build has
# to produce exactly the same pair the player copies onto the device. Point
# OPENQ4_PAK_DIR at a desktop builddir/baseoq4 to reuse those instead; pak1 is
# a couple of gigabytes and takes a few minutes to pack the first time.
if(NOT DEFINED OPENQ4_PAK_DIR)
    set(OPENQ4_PAK_DIR "${CMAKE_CURRENT_BINARY_DIR}/baseoq4")
endif()
file(MAKE_DIRECTORY "${OPENQ4_PAK_DIR}")

# Most of pak1 cannot be reached from this build, so Android packs a reduced
# one. Two thirds of it is the baked light grid, and draw_common.cpp -- the only
# file that renders it -- is in RENDERER_GLES_EXCLUDED_SOURCES, so the GLES
# module has no code path that could read it. The rest is the upscaled
# loadscreens, which retail q4base already covers for 45 of the 46 maps; mcc_1
# is the exception and stays, and generic.* is required by the packer and
# named directly by the engine.
#
# 561MB becomes 1.7MB. Set OPENQ4_MINIMAL_PAK1 to OFF for a desktop-identical
# pair. The engine's checksum test follows either way: the generated header
# takes its MD5 from whatever was packed here.
option(OPENQ4_MINIMAL_PAK1 "Pack only the pak1 content the GLES build can use" ON)

set(OPENQ4_PAK1_EXCLUDES "")
if(OPENQ4_MINIMAL_PAK1)
    set(OPENQ4_PAK1_EXCLUDES
        --exclude "gfx/guis/loadscreens/*"
        --exclude "!gfx/guis/loadscreens/generic.*"
        --exclude "!gfx/guis/loadscreens/mcc_1*"
        --exclude "env/*"
        --exclude "maps/*")
endif()

function(oq4_ensure_pak PAK_NAME SOURCE_SUBDIR)
    set(_pak "${OPENQ4_PAK_DIR}/${PAK_NAME}")
    # The filter set is part of the pack's identity, so a stamp beside it forces
    # a repack when it changes. Without this, flipping OPENQ4_MINIMAL_PAK1 would
    # silently keep whichever pack happened to be there and then bake its MD5
    # into the header, which is a fatal error at runtime rather than a warning.
    set(_stamp "${_pak}.filters")
    string(REPLACE ";" " " _filter_text "${ARGN}")
    set(_existing "")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _existing)
    endif()
    if(EXISTS "${_pak}" AND _existing STREQUAL _filter_text)
        return()
    endif()
    message(STATUS "openQ4: packing ${PAK_NAME}")
    oq4_run_script(_manifest_out "${OQ4_ROOT}/tools/build/write_pak_manifest.py"
        "${OQ4_ROOT}" "${PAK_NAME}" "${SOURCE_SUBDIR}" "${OQ4_GEN}/${PAK_NAME}.sources")
    oq4_run_script(_pack_out "${OQ4_ROOT}/tools/build/build_openq4_pack.py"
        --pak-name "${PAK_NAME}"
        --source-dir "${OQ4_ROOT}/${SOURCE_SUBDIR}"
        --manifest "${OQ4_GEN}/${PAK_NAME}.sources"
        --out "${_pak}"
        ${ARGN})
    file(WRITE "${_stamp}" "${_filter_text}")
endfunction()

oq4_ensure_pak(pak0.pk4 content/baseoq4/pak0)
oq4_ensure_pak(pak1.pk4 content/baseoq4/pak1 ${OPENQ4_PAK1_EXCLUDES})

oq4_run_script(_paks_out "${OQ4_ROOT}/tools/build/generate_pak_header.py"
    --pak0 "${OPENQ4_PAK_DIR}/pak0.pk4"
    --pak1 "${OPENQ4_PAK_DIR}/pak1.pk4"
    --header-out "${OQ4_GEN}/openq4_paks_generated.h")

# baseoq4/mod.json is as required as the pk4s: idFileSystemLocal::Startup
# fatal-errors when fs_game is the openQ4 game dir and its manifest is missing
# or its requiredopenQ4Version does not match the engine. Meson generates it in
# content/baseoq4/meson.build; do the same here so the staged directory is
# complete rather than pk4-only.
string(REGEX MATCH "base_version=([^\r\n]*)" _version_match "${_version_out}")
set(OPENQ4_VERSION_BASE "${CMAKE_MATCH_1}")
if(NOT OPENQ4_VERSION_BASE)
    message(FATAL_ERROR "openq4_version.py did not report base_version:\n${_version_out}")
endif()
configure_file("${OQ4_ROOT}/content/baseoq4/mod.json.in" "${OPENQ4_PAK_DIR}/mod.json" @ONLY)

# The host app ships baseoq4 inside the APK and unpacks it on the device, so
# copy it wherever the app wants it. Set from the host build, because only it
# knows its own asset layout; unset, this does nothing and the files stay in the
# build directory.
#
# All three travel together and this runs after all three exist. Two of them are
# separately fatal if they go missing or stale: the engine compares each pak
# against the MD5 compiled into it, and refuses to start without a mod.json
# whose requiredopenQ4Version matches. Staging here, rather than in a build
# target, keeps them in step with the header generated above.
if(OPENQ4_PAK_ASSET_DIR)
    file(MAKE_DIRECTORY "${OPENQ4_PAK_ASSET_DIR}")
    foreach(_staged_name pak0.pk4 pak1.pk4 mod.json)
        file(COPY_FILE
            "${OPENQ4_PAK_DIR}/${_staged_name}"
            "${OPENQ4_PAK_ASSET_DIR}/${_staged_name}"
            ONLY_IF_DIFFERENT)
    endforeach()
    message(STATUS "openQ4: staged pak0.pk4, pak1.pk4 and mod.json into ${OPENQ4_PAK_ASSET_DIR}")
endif()

# ---------------------------------------------------------------------------
# Shared compile settings (the CMake peer of meson.build's shared_*_args)
# ---------------------------------------------------------------------------

set(OQ4_SHARED_DEFS USE_OPENAL GLEW_NO_GLU ID_GL_HARDLINK USE_SDL3=1)

# -fpermissive: idTech4-era implicit narrowing/pointer casts.
# -ffp-contract=off: keeps arm64 FMA contraction from changing collision, AAS
# and physics results relative to the validated x86-64 SSE2 baseline, which is
# what savegame and map compatibility depend on.
set(OQ4_SHARED_FLAGS -fpermissive -fno-strict-aliasing -ffp-contract=off)

set(OQ4_ENGINE_INCLUDES
    "${OQ4_ROOT}"
    "${OQ4_GEN}"
    "${OQ4_SRC}"
    "${OQ4_SRC}/external/stb_vorbis"
    "${OPENQ4_GAMELIBS_DIR}/src/game"
    "${OPENAL_INCLUDE_DIR}")

set(OQ4_GAME_INCLUDES
    "${OQ4_ROOT}"
    "${OQ4_GEN}"
    "${OQ4_SRC}"
    "${OQ4_SRC}/idlib"
    "${OQ4_SRC}/renderer"
    "${OQ4_SRC}/ui"
    "${OQ4_SRC}/sys"
    "${OQ4_SRC}/bse"
    "${OQ4_SRC}/MayaImport"
    "${OPENQ4_GAMELIBS_DIR}/src/game")

set(OQ4_PRECOMPILED_HEADER "${OQ4_SRC}/idlib/precompiled.h")

# Every legacy engine and SDK translation unit uses precompiled.h as its common
# prefix; the desktop build force-includes it the same way.
function(oq4_apply_common TARGET)
    target_compile_definitions(${TARGET} PRIVATE ${OQ4_SHARED_DEFS})
    target_compile_options(${TARGET} PRIVATE ${OQ4_SHARED_FLAGS})
    target_precompile_headers(${TARGET} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${OQ4_PRECOMPILED_HEADER}>")
endfunction()

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Static libraries shared between the engine and the renderer module
# ---------------------------------------------------------------------------

# AL_SoundSample.cpp includes stb_vorbis.c header-only, so the implementation
# needs its own translation unit (the desktop build gets it from the
# subprojects/stb_vorbis wrap).
add_library(openq4_stb_vorbis STATIC "${OQ4_SRC}/external/stb_vorbis/stb_vorbis.c")
target_include_directories(openq4_stb_vorbis PRIVATE "${OQ4_SRC}/external/stb_vorbis")

add_library(openq4_bse STATIC ${OQ4_BSE_SOURCES})
target_include_directories(openq4_bse PRIVATE ${OQ4_ENGINE_INCLUDES} "${OQ4_SRC}/bse")
oq4_apply_common(openq4_bse)

add_library(openq4_imagetools STATIC ${OQ4_IMAGETOOLS_SOURCES})
target_include_directories(openq4_imagetools PRIVATE ${OQ4_ENGINE_INCLUDES})
target_compile_definitions(openq4_imagetools PRIVATE __DOOM_DLL__)
oq4_apply_common(openq4_imagetools)

add_library(openq4_render_geo STATIC ${OQ4_RENDER_GEO_SOURCES})
target_include_directories(openq4_render_geo PRIVATE ${OQ4_ENGINE_INCLUDES})
target_compile_definitions(openq4_render_geo PRIVATE __DOOM_DLL__)
oq4_apply_common(openq4_render_geo)

# The renderer module's own idlib copy, engine-flavoured so its codegen and
# interface layout match the renderer sources it is linked into.
add_library(openq4_renderer_idlib STATIC ${OQ4_IDLIB_SOURCES})
target_include_directories(openq4_renderer_idlib PRIVATE ${OQ4_ENGINE_INCLUDES})
target_compile_definitions(openq4_renderer_idlib PRIVATE __DOOM_DLL__)
oq4_apply_common(openq4_renderer_idlib)

# The game module's idlib copy. Separate archive because precompiled.h pulls in
# the game's own types under -DGAME_DLL, so sharing one archive would put two
# different definitions of the same types into one binary.
add_library(openq4_game_idlib STATIC ${OQ4_IDLIB_SOURCES})
target_include_directories(openq4_game_idlib PRIVATE
    "${OQ4_ROOT}" "${OQ4_GEN}" "${OQ4_SRC}" "${OPENQ4_GAMELIBS_DIR}/src/game")
target_compile_definitions(openq4_game_idlib PRIVATE GAME_DLL)
set_target_properties(openq4_game_idlib PROPERTIES
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden)
oq4_apply_common(openq4_game_idlib)

# ---------------------------------------------------------------------------
# libquake4.so: the engine client plus the OpenTouch host glue
# ---------------------------------------------------------------------------

set(OQ4_MOBILE_SOURCES
    "${OQ4_ROOT}/mobile/game_interface.cpp"
    "${OQ4_ROOT}/mobile/touch_interface_quake4.cpp"
    "${CLIBS_DIR}/sigma/android_jni.cpp"
    "${CLIBS_DIR}/touch_interface_base.cpp"
    "${CLIBS_DIR}/LogWritter.c"
    "${CLIBS_DIR}/fts.c"
    "${CLIBS_DIR}/rand.c")

# The host glue is not engine code and must not get precompiled.h forced into
# it - that prefix pulls in the whole engine and collides with the touch
# library's own names.
set_source_files_properties(${OQ4_MOBILE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_library(quake4 SHARED ${OQ4_ENGINE_SOURCES} ${OQ4_MOBILE_SOURCES})
target_include_directories(quake4 PRIVATE
    # mobile/ must precede CLIBS_DIR so the shared touch_interface_base.cpp and
    # JNI glue resolve *this* engine's touch_interface.h, not another engine's.
    "${OQ4_ROOT}/mobile"
    ${OQ4_ENGINE_INCLUDES}
    "${SDL3_INCLUDE_DIR}"
    "${CLIBS_DIR}"
    "${TOUCH_CONTROLS_DIR}")
target_compile_definitions(quake4 PRIVATE
    __DOOM_DLL__
    OPENQ4_RENDERER_MODULE_ONLY
    # names the touch-control layout XML files
    ENGINE_NAME="quake4")
target_link_libraries(quake4
    openq4_bse openq4_imagetools openq4_render_geo openq4_stb_vorbis
    "${SDL3_LIBRARY}" openal-soft touchcontrols saffal
    log android dl m)
oq4_apply_common(quake4)
set_target_properties(quake4 PROPERTIES PREFIX "lib" OUTPUT_NAME "quake4")

# ---------------------------------------------------------------------------
# librenderer-gles_arm64.so: the ES 3.0 renderer module
# ---------------------------------------------------------------------------

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR ANDROID_ABI STREQUAL "arm64-v8a")
    set(OQ4_MODULE_ARCH_TAG "arm64")
else()
    # sys_local.cpp / Common.cpp / RendererModule.cpp all derive the same tag
    # from the compiler's arch macros; anything else has no tag they agree on.
    message(FATAL_ERROR "openQ4 on Android targets arm64-v8a only (got ${ANDROID_ABI}).")
endif()

add_library(renderer-gles_${OQ4_MODULE_ARCH_TAG} SHARED ${OQ4_RENDERER_GLES_SOURCES})
set(OQ4_RENDERER_GLES_TARGET renderer-gles_${OQ4_MODULE_ARCH_TAG})
target_include_directories(${OQ4_RENDERER_GLES_TARGET} PRIVATE
    ${OQ4_ENGINE_INCLUDES}
    "${OQ4_SRC}/external/gles")
target_compile_definitions(${OQ4_RENDERER_GLES_TARGET} PRIVATE
    __DOOM_DLL__
    OPENQ4_RENDERER_MODULE
    OPENQ4_RENDERER_GLES_MODULE
    OPENQ4_SDL3_CONTEXT_IMPL
    OPENQ4_RENDERER_BACKEND_NAME="gles"
    OPENQ4_RENDERER_BACKEND_DESC="openQ4 OpenGL ES renderer module")
target_link_libraries(${OQ4_RENDERER_GLES_TARGET}
    openq4_renderer_idlib openq4_imagetools openq4_render_geo
    GLESv3 EGL log m)
oq4_apply_common(${OQ4_RENDERER_GLES_TARGET})
set_target_properties(${OQ4_RENDERER_GLES_TARGET} PROPERTIES
    PREFIX "lib"
    CXX_VISIBILITY_PRESET hidden)

# ---------------------------------------------------------------------------
# libgame-sp_arm64.so: the single-player game module
# ---------------------------------------------------------------------------

add_library(game-sp_${OQ4_MODULE_ARCH_TAG} SHARED ${OQ4_GAME_SP_SOURCES})
set(OQ4_GAME_SP_TARGET game-sp_${OQ4_MODULE_ARCH_TAG})
target_include_directories(${OQ4_GAME_SP_TARGET} PRIVATE ${OQ4_GAME_INCLUDES})
target_compile_definitions(${OQ4_GAME_SP_TARGET} PRIVATE GAME_DLL)
target_link_libraries(${OQ4_GAME_SP_TARGET} openq4_game_idlib log m)
oq4_apply_common(${OQ4_GAME_SP_TARGET})
set_target_properties(${OQ4_GAME_SP_TARGET} PROPERTIES
    PREFIX "lib"
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden)

# The engine dlopen()s both modules at runtime, but they must be built (and
# therefore packaged) whenever the engine is.
add_dependencies(quake4 ${OQ4_RENDERER_GLES_TARGET} ${OQ4_GAME_SP_TARGET})
