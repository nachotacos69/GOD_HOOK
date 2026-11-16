cmake_minimum_required(VERSION 3.20)

# Default to the same bundled GCC toolchain as build-gcc11.cmd.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/psp-toolchain.cmake"
        CACHE FILEPATH "PSP cross-compilation toolchain")
endif()

project(GEBHookProbe VERSION 3.7.8 LANGUAGES C ASM)
option(GEB_ENABLE_LOGGING "Build debug file logging (stable leaves this OFF)" OFF)
unset(GEB_ENABLE_RENDERING CACHE)
message(STATUS "GEB: rendering always ON; debug logging=${GEB_ENABLE_LOGGING}; runtime auto-detect")
configure_file(plugin.ini "${CMAKE_CURRENT_BINARY_DIR}/plugin.ini" COPYONLY)

if(NOT PSPDEV_ROOT OR NOT PSP_SDK_ROOT)
    message(FATAL_ERROR "Use cmake/psp-toolchain.cmake to select the PSP SDK.")
endif()
if(NOT EXISTS "${PSP_SDK_ROOT}/lib/linkfile.prx")
    message(FATAL_ERROR "PSP linker script not found in ${PSP_SDK_ROOT}.")
endif()

find_program(PSP_BUILD_EXPORTS NAMES psp-build-exports
    PATHS "${PSPDEV_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(PSP_FIXUP_IMPORTS NAMES psp-fixup-imports
    PATHS "${PSPDEV_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(PSP_PRXGEN NAMES psp-prxgen
    PATHS "${PSPDEV_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)

set(_exports "${CMAKE_CURRENT_BINARY_DIR}/exports.c")
add_custom_command(
    OUTPUT "${_exports}"
    COMMAND "${CMAKE_COMMAND}"
        "-DEXPORT_TOOL=${PSP_BUILD_EXPORTS}"
        "-DEXPORT_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/exports.exp"
        "-DEXPORT_OUTPUT=${_exports}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateExports.cmake"
    DEPENDS src/exports.exp cmake/GenerateExports.cmake "${PSP_BUILD_EXPORTS}"
    COMMENT "Generating PSP module exports"
    VERBATIM)

add_executable(geb_hook_probe
    src/main.c
    src/runtime.c
    src/hook.c
    src/tr2.c
    src/overlay.c
    src/overlay_user.S
    src/log.c
    src/log_format.c
    "${_exports}")
set_target_properties(geb_hook_probe PROPERTIES
    SUFFIX ".elf"
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES)
set_source_files_properties(src/overlay_user.S PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/overlay_layout.h")

target_include_directories(geb_hook_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PSPDEV_ROOT}/psp/include"
    "${PSP_SDK_ROOT}/include")
target_compile_definitions(geb_hook_probe PRIVATE
    _PSP_FW_VERSION=150
    GEB_PROBE_VERSION="${PROJECT_VERSION}"
    GEB_ENABLE_LOGGING=$<BOOL:${GEB_ENABLE_LOGGING}>)
# Kernel Sysclib is not ISO libc: PPSSPP's memset has a zero return value.
# Callers discard it; do not let GCC substitute it for the destination pointer.
target_compile_options(geb_hook_probe PRIVATE
    -O2 -G0 -Wall -Wextra -Werror -Wno-discarded-array-qualifiers
    -fno-pic -fno-strict-aliasing -fno-builtin-printf -fno-builtin-memset)
target_link_directories(geb_hook_probe PRIVATE
    "${PSPDEV_ROOT}/psp/lib"
    "${PSP_SDK_ROOT}/lib")
target_link_options(geb_hook_probe PRIVATE
    "-Wl,-q,-T${PSP_SDK_ROOT}/lib/linkfile.prx"
    -nostartfiles -nostdlib -Wl,-zmax-page-size=128)
target_link_libraries(geb_hook_probe PRIVATE
    pspdebug pspdisplay_driver pspctrl_driver pspmodinfo pspsdk pspkernel)
set_property(TARGET geb_hook_probe APPEND PROPERTY
    LINK_DEPENDS "${PSP_SDK_ROOT}/lib/linkfile.prx" "${PSP_FIXUP_IMPORTS}")

# build_prx.mak fixes the ELF imports before converting it to a PRX.
add_custom_command(TARGET geb_hook_probe POST_BUILD
    COMMAND "${PSP_FIXUP_IMPORTS}" "$<TARGET_FILE:geb_hook_probe>"
    COMMENT "Fixing PSP imports"
    VERBATIM)
set(_prx "${CMAKE_CURRENT_BINARY_DIR}/geb_hook.prx")
add_custom_command(
    OUTPUT "${_prx}"
    COMMAND "${PSP_PRXGEN}" "$<TARGET_FILE:geb_hook_probe>" "${_prx}"
    DEPENDS geb_hook_probe "$<TARGET_FILE:geb_hook_probe>" "${PSP_PRXGEN}"
    COMMENT "Generating geb_hook.prx"
    VERBATIM)
add_custom_target(prx ALL DEPENDS "${_prx}")
