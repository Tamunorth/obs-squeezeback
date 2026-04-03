cmake_minimum_required(VERSION 3.16)

project(obs-squeezeback VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(PLUGIN_NAME "obs-squeezeback")

# ── Auto-detect OBS installation ──

# Check registry first, then common paths
if(NOT OBS_DIR)
    # Try registry
    execute_process(
        COMMAND reg query "HKLM\\SOFTWARE\\OBS Studio" /ve
        OUTPUT_VARIABLE REG_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(REG_OUTPUT MATCHES "REG_SZ[ \t]+(.+)")
        string(STRIP "${CMAKE_MATCH_1}" OBS_DIR)
    endif()
endif()

if(NOT OBS_DIR)
    # Try common paths
    foreach(_path
        "C:/Program Files/obs-studio"
        "C:/Program Files (x86)/obs-studio"
        "$ENV{ProgramFiles}/obs-studio"
    )
        if(EXISTS "${_path}/bin/64bit/obs.dll")
            set(OBS_DIR "${_path}")
            break()
        endif()
    endforeach()
endif()

if(NOT OBS_DIR OR NOT EXISTS "${OBS_DIR}/bin/64bit/obs.dll")
    message(FATAL_ERROR
        "Could not find OBS Studio installation.\n"
        "Set -DOBS_DIR=\"C:/path/to/obs-studio\" manually.")
endif()

message(STATUS "Found OBS at: ${OBS_DIR}")

# ── OBS SDK headers (downloaded from GitHub) ──

set(OBS_SDK_DIR "${CMAKE_BINARY_DIR}/obs-sdk")
set(OBS_VERSION "32.0.2")

if(NOT EXISTS "${OBS_SDK_DIR}/libobs/obs-module.h")
    message(STATUS "Downloading OBS ${OBS_VERSION} headers...")
    file(DOWNLOAD
        "https://github.com/obsproject/obs-studio/archive/refs/tags/${OBS_VERSION}.tar.gz"
        "${CMAKE_BINARY_DIR}/obs-src.tar.gz"
        SHOW_PROGRESS
        STATUS DL_STATUS
    )
    list(GET DL_STATUS 0 DL_CODE)
    if(NOT DL_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download OBS source: ${DL_STATUS}")
    endif()

    file(ARCHIVE_EXTRACT
        INPUT "${CMAKE_BINARY_DIR}/obs-src.tar.gz"
        DESTINATION "${CMAKE_BINARY_DIR}/obs-src-extract"
    )

    # Copy only the headers we need
    file(COPY "${CMAKE_BINARY_DIR}/obs-src-extract/obs-studio-${OBS_VERSION}/libobs/"
         DESTINATION "${OBS_SDK_DIR}/libobs/"
         FILES_MATCHING
         PATTERN "*.h"
         PATTERN "*.hpp"
    )

    # Generate obsconfig.h (build-time generated header)
    file(WRITE "${OBS_SDK_DIR}/libobs/obsconfig.h"
        "#pragma once\n"
        "#define OBS_DATA_PATH \"data\"\n"
        "#define OBS_PLUGIN_PATH \"obs-plugins/64bit\"\n"
        "#define OBS_PLUGIN_DESTINATION \"obs-plugins/64bit\"\n"
        "#define OBS_RELEASE_CANDIDATE 0\n"
        "#define OBS_BETA 0\n"
    )

    message(STATUS "OBS headers extracted to ${OBS_SDK_DIR}")
endif()

# Copy frontend API header from OBS source
set(OBS_FRONTEND_HEADER "${OBS_SDK_DIR}/obs-frontend-api.h")
if(NOT EXISTS "${OBS_FRONTEND_HEADER}")
    set(_frontend_src "${CMAKE_BINARY_DIR}/obs-src-extract/obs-studio-${OBS_VERSION}/frontend/api/obs-frontend-api.h")
    if(EXISTS "${_frontend_src}")
        file(COPY "${_frontend_src}" DESTINATION "${OBS_SDK_DIR}")
        message(STATUS "Copied obs-frontend-api.h to SDK dir")
    else()
        message(WARNING "obs-frontend-api.h not found in OBS source")
    endif()
endif()

# ── Find Qt6 ──

find_package(Qt6 REQUIRED COMPONENTS Widgets)
message(STATUS "Found Qt6 at: ${Qt6_DIR}")

# ── Generate import library from obs.dll ──

set(OBS_LIB_DIR "${CMAKE_BINARY_DIR}/obs-lib")
file(MAKE_DIRECTORY "${OBS_LIB_DIR}")

if(MSVC AND NOT EXISTS "${OBS_LIB_DIR}/obs.lib")
    message(STATUS "Generating import library from obs.dll...")
    message(STATUS "  Run this if cmake cannot find lib.exe automatically:")
    message(STATUS "  > generate_lib.bat  (from the project root)")

    # Find MSVC tools
    find_program(DUMPBIN_EXE dumpbin)
    find_program(LIB_EXE lib)

    if(DUMPBIN_EXE AND LIB_EXE)
        execute_process(
            COMMAND ${DUMPBIN_EXE} /exports "${OBS_DIR}/bin/64bit/obs.dll"
            OUTPUT_FILE "${OBS_LIB_DIR}/obs_exports.txt"
            ERROR_QUIET
        )

        file(STRINGS "${OBS_LIB_DIR}/obs_exports.txt" EXPORT_LINES)
        set(DEF_CONTENT "LIBRARY obs\nEXPORTS\n")
        foreach(LINE ${EXPORT_LINES})
            if(LINE MATCHES "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t]+)")
                string(APPEND DEF_CONTENT "    ${CMAKE_MATCH_1}\n")
            endif()
        endforeach()

        file(WRITE "${OBS_LIB_DIR}/obs.def" "${DEF_CONTENT}")

        execute_process(
            COMMAND ${LIB_EXE} /def:"${OBS_LIB_DIR}/obs.def"
                /out:"${OBS_LIB_DIR}/obs.lib" /machine:x64
            WORKING_DIRECTORY "${OBS_LIB_DIR}"
            ERROR_QUIET
        )
    endif()

    # Also generate obs-frontend-api.lib
    if(NOT EXISTS "${OBS_LIB_DIR}/obs-frontend-api.lib")
        execute_process(
            COMMAND ${DUMPBIN_EXE} /exports "${OBS_DIR}/bin/64bit/obs-frontend-api.dll"
            OUTPUT_FILE "${OBS_LIB_DIR}/frontend_exports.txt"
            ERROR_QUIET
        )

        file(STRINGS "${OBS_LIB_DIR}/frontend_exports.txt" FE_EXPORT_LINES)
        set(FE_DEF_CONTENT "LIBRARY obs-frontend-api\nEXPORTS\n")
        foreach(LINE ${FE_EXPORT_LINES})
            if(LINE MATCHES "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t]+)")
                string(APPEND FE_DEF_CONTENT "    ${CMAKE_MATCH_1}\n")
            endif()
        endforeach()

        file(WRITE "${OBS_LIB_DIR}/obs-frontend-api.def" "${FE_DEF_CONTENT}")

        execute_process(
            COMMAND ${LIB_EXE} /def:"${OBS_LIB_DIR}/obs-frontend-api.def"
                /out:"${OBS_LIB_DIR}/obs-frontend-api.lib" /machine:x64
            WORKING_DIRECTORY "${OBS_LIB_DIR}"
            ERROR_QUIET
        )

        if(EXISTS "${OBS_LIB_DIR}/obs-frontend-api.lib")
            message(STATUS "Generated obs-frontend-api.lib")
        else()
            message(WARNING "Could not generate obs-frontend-api.lib")
        endif()
    endif()

    if(NOT EXISTS "${OBS_LIB_DIR}/obs.lib")
        message(WARNING
            "Could not auto-generate obs.lib.\n"
            "Run generate_lib.bat from a VS Developer Command Prompt first,\n"
            "then re-run cmake.")
    else()
        message(STATUS "Generated obs.lib")
    endif()
endif()

# ── Plugin target ──

add_library(${PLUGIN_NAME} MODULE
    src/plugin-main.c
    src/squeezeback-transition.c
    src/squeezeback-filter.c
    src/squeezeback-dock.cpp
    src/squeezeback-dock-bridge.h
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    "${OBS_SDK_DIR}"
    "${OBS_SDK_DIR}/libobs"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Link to OBS + frontend API + Qt6
if(EXISTS "${OBS_LIB_DIR}/obs.lib")
    target_link_libraries(${PLUGIN_NAME} PRIVATE "${OBS_LIB_DIR}/obs.lib")
else()
    target_link_libraries(${PLUGIN_NAME} PRIVATE "${OBS_DIR}/bin/64bit/obs.dll")
endif()

if(EXISTS "${OBS_LIB_DIR}/obs-frontend-api.lib")
    target_link_libraries(${PLUGIN_NAME} PRIVATE "${OBS_LIB_DIR}/obs-frontend-api.lib")
else()
    target_link_libraries(${PLUGIN_NAME} PRIVATE "${OBS_DIR}/bin/64bit/obs-frontend-api.dll")
endif()

target_link_libraries(${PLUGIN_NAME} PRIVATE Qt6::Widgets)

set_target_properties(${PLUGIN_NAME} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    PREFIX ""  # No 'lib' prefix on output
)

# ── Install target: copies directly into OBS plugins directory ──

set(OBS_PLUGIN_DIR "${OBS_DIR}/obs-plugins/64bit")
set(OBS_DATA_DIR "${OBS_DIR}/data/obs-plugins/${PLUGIN_NAME}")

install(TARGETS ${PLUGIN_NAME}
    RUNTIME DESTINATION "${OBS_PLUGIN_DIR}"
    LIBRARY DESTINATION "${OBS_PLUGIN_DIR}"
)

install(DIRECTORY data/
    DESTINATION "${OBS_DATA_DIR}"
)

# ── Convenience: custom "install-plugin" target ──

add_custom_target(install-plugin
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:${PLUGIN_NAME}>"
        "${OBS_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PLUGIN_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OBS_DATA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${OBS_DATA_DIR}"
    DEPENDS ${PLUGIN_NAME}
    COMMENT "Installing ${PLUGIN_NAME} to OBS at ${OBS_DIR}"
)

message(STATUS "Plugin will install to: ${OBS_PLUGIN_DIR}")
message(STATUS "Data will install to:   ${OBS_DATA_DIR}")
