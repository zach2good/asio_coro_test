cmake_minimum_required(VERSION 3.28)

project(HeaderOnlyBoost LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(LINKER_LANGUAGE CXX)
set(USE_FOLDERS ON)

# Generate compile_commands.json to make it easier to work with clang based tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "CMAKE_VERSION: ${CMAKE_VERSION}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_CXX_STANDARD: ${CMAKE_CXX_STANDARD}")
message(STATUS "CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
message(STATUS "CMAKE_GENERATOR_PLATFORM: ${CMAKE_GENERATOR_PLATFORM}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

set(BOOST_VERSION_TAG boost-1.86.0)
set(BOOST_COMPONENTS
    config
    core
    static_assert
    winapi
    system
    assert
    throw_exception
    variant2
    mp11
    predef
    endian
    align
    optional
    type_traits
    move
    utility
)
foreach(component ${BOOST_COMPONENTS})
    CPMAddPackage(
        NAME Boost.${component}
        GITHUB_REPOSITORY boostorg/${component}
        GIT_TAG ${BOOST_VERSION_TAG}
        SYSTEM TRUE
    )
endforeach()

add_library(BoostHeaders INTERFACE)
foreach(component ${BOOST_COMPONENTS})
    target_include_directories(BoostHeaders INTERFACE ${Boost.${component}_SOURCE_DIR}/include)
endforeach()

target_compile_definitions(BoostHeaders INTERFACE
    BOOST_ASIO_DISABLE_BOOST_DATE_TIME
    BOOST_ASIO_NO_DEPRECATED
    _WIN32_WINNT=0x0601
)

# We want to use a different and more modern version of Boost.ASIO (1.87.0.beta1)
CPMAddPackage(
    NAME Boost.ASIO
    GITHUB_REPOSITORY boostorg/asio
    GIT_TAG boost-1.87.0.beta1
    SYSTEM TRUE
)
target_include_directories(BoostHeaders INTERFACE ${Boost.ASIO_SOURCE_DIR}/include)

# We want to use a different and more modern version of Boost.MySQL (1.87.0.beta1)
CPMAddPackage(
    NAME Boost.MySQL
    GITHUB_REPOSITORY boostorg/mysql
    GIT_TAG boost-1.87.0.beta1
    SYSTEM TRUE
)
target_include_directories(BoostHeaders INTERFACE ${Boost.MySQL_SOURCE_DIR}/include)

# We want to use a different and slightly older version of Boost.CharConv (1.85.0)
CPMAddPackage(
    NAME Boost.CharConv
    GITHUB_REPOSITORY boostorg/charconv
    GIT_TAG boost-1.85.0
    SYSTEM TRUE
)
add_library(BoostCompilationUnits ALIAS boost_charconv)

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

add_executable(main main.cpp)

target_link_libraries(main PRIVATE Threads::Threads OpenSSL::SSL OpenSSL::Crypto BoostHeaders BoostCompilationUnits TracyClient fmt::fmt spdlog)
