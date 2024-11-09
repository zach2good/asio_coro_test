cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(asio_coro_test LANGUAGES CXX)

add_executable(${PROJECT_NAME} main.cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

find_package(Threads REQUIRED)

CPMAddPackage(
    NAME asio
    VERSION 1.32.0
    GITHUB_REPOSITORY chriskohlhoff/asio
    GIT_TAG asio-1-32-0 # asio uses non-standard version tag, we must specify GIT_TAG
) # defines asio

# ASIO doesn't use CMake, we have to configure it manually. Extra notes for using on Windows:
#
# If _WIN32_WINNT is not set, ASIO assumes _WIN32_WINNT=0x0501, i.e. Windows XP target, which is
# definitely not the platform which most users target.
if(asio_ADDED)
    add_library(asio INTERFACE)

    target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)

    target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

    target_link_libraries(asio INTERFACE Threads::Threads)

    if(WIN32)
        # macro see @ https://stackoverflow.com/a/40217291/1746503
        macro(get_win32_winnt version)
            if(CMAKE_SYSTEM_VERSION)
                set(ver ${CMAKE_SYSTEM_VERSION})
                string(REGEX MATCH "^([0-9]+).([0-9])" ver ${ver})
                string(REGEX MATCH "^([0-9]+)" verMajor ${ver})

                # Check for Windows 10, b/c we'll need to convert to hex 'A'.
                if("${verMajor}" MATCHES "10")
                    set(verMajor "A")
                    string(REGEX REPLACE "^([0-9]+)" ${verMajor} ver ${ver})
                endif("${verMajor}" MATCHES "10")

                # Remove all remaining '.' characters.
                string(REPLACE "." "" ver ${ver})

                # Prepend each digit with a zero.
                string(REGEX REPLACE "([0-9A-Z])" "0\\1" ver ${ver})
                set(${version} "0x${ver}")
            endif()
        endmacro()

        if(NOT DEFINED _WIN32_WINNT)
            get_win32_winnt(ver)
            set(_WIN32_WINNT ${ver})
        endif()

        message(STATUS "Set _WIN32_WINNET=${_WIN32_WINNT}")

        target_compile_definitions(asio INTERFACE _WIN32_WINNT=${_WIN32_WINNT})
    endif()
endif()

option(TRACY_ENABLE "Enable Tracy profiling." ON)

# Also handle close flags:
if(ENABLE_TRACY OR TRACY_ENABLED OR TRACY)
    set(TRACY_ENABLE ON)
endif()

message(STATUS "TRACY_ENABLE: ${TRACY_ENABLE}")

# CPM Modules
if(TRACY_ENABLE)
    # Tracy version tag, without the leading 'v'
    set(TRACY_VERSION 0.11.1)

    # Download client library
    CPMAddPackage(
        NAME TracyClient
        GITHUB_REPOSITORY wolfpld/tracy
        GIT_TAG v${TRACY_VERSION}
        OPTIONS
        "TRACY_ENABLE ON"
        "TRACY_ON_DEMAND OFF"
        "TRACY_CALLSTACK ON" # Value hardcoded to 4 in common/tracy.h
        "TRACY_NO_BROADCAST ON"
        "TRACY_NO_CONTEXT_SWITCH ON"
        "TRACY_NO_EXIT ON"
        "TRACY_NO_VSYNC_CAPTURE ON"
        "TRACY_NO_FRAME_IMAGE ON"

        # "TRACY_NO_SYSTEM_TRACING ON" # This is incredibly heavy, add this line back if you need to speed up profiling
        # "TRACY_TIMER_FALLBACK OFF" # Maybe useful to set ON for Linux VMs?
        "TRACY_LIBBACKTRACE_ELF_DYNLOAD_SUPPORT ON"
        "TRACY_FIBERS ON"
    )

    # Download Win32 server executables
    message(STATUS "Downloading Tracy client")
    file(DOWNLOAD
        https://github.com/wolfpld/tracy/releases/download/v${TRACY_VERSION}/windows-${TRACY_VERSION}.zip
        ${CMAKE_SOURCE_DIR}/tracy.tar.gz
        TIMEOUT 60
    )
    execute_process(COMMAND "${CMAKE_COMMAND}" -E
        tar xvf "${CMAKE_SOURCE_DIR}/tracy.tar.gz"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/
    )
endif(TRACY_ENABLE)

CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 10.1.1
) # defines: fmt::fmt

CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.12.0
    OPTIONS
    "SPDLOG_ENABLE_PCH ON"
    "SPDLOG_FMT_EXTERNAL ON"
) # defines: spdlog

target_link_libraries(${PROJECT_NAME} asio TracyClient fmt::fmt spdlog)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
