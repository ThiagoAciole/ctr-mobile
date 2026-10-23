cmake_minimum_required(VERSION 3.20)
project(CTR-Mobile C CXX)
include(CheckCCompilerFlag)

if(NOT ANDROID)
    message(FATAL_ERROR "CTR Mobile supports Android builds only. Open the android/ project in Android Studio.")
endif()

set(CTR_NATIVE_VERSION "0.1.0-beta.7.1")

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error -g")

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# SDL3
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR ON CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER ON CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)
target_link_libraries(SDL3-shared PRIVATE c++_shared)

# CTR Native
add_library(ctr_native SHARED main.c)
set_target_properties(ctr_native PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)

# ENet, vendored. OnlineCTR speaks ENet, so a compatible client needs the same
# transport; it is a small zlib-licensed library with no dependencies of its own,
# which is why it is checked in rather than fetched.
add_library(enet STATIC
    ${CMAKE_SOURCE_DIR}/externals/enet/callbacks.c
    ${CMAKE_SOURCE_DIR}/externals/enet/compress.c
    ${CMAKE_SOURCE_DIR}/externals/enet/host.c
    ${CMAKE_SOURCE_DIR}/externals/enet/list.c
    ${CMAKE_SOURCE_DIR}/externals/enet/packet.c
    ${CMAKE_SOURCE_DIR}/externals/enet/peer.c
    ${CMAKE_SOURCE_DIR}/externals/enet/protocol.c
    ${CMAKE_SOURCE_DIR}/externals/enet/unix.c
    ${CMAKE_SOURCE_DIR}/externals/enet/win32.c)
target_include_directories(enet PUBLIC ${CMAKE_SOURCE_DIR}/externals/enet/include)
target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1)
target_compile_options(enet PRIVATE -w)
if(NOT WIN32)
    target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1)
endif()

# libchdr, vendored. CHD is what most people actually keep a PS1 disc as, and
# reading one needs real decompressors -- LZMA, zlib, zstd and FLAC -- so this is
# a library rather than something to hand-roll. It ships a single unity.c that
# pulls in its own dependencies, which is why it is one source file here.
# BSD-3-Clause, compatible with this project's GPLv3.
# unity.c omits the A/V codec even though the codec table references it, so it
# is compiled alongside. It is for LaserDisc-style CHDs and does nothing for a
# CD, but the table will not link without it.
add_library(chdr STATIC
    ${CMAKE_SOURCE_DIR}/externals/libchdr/unity.c
    ${CMAKE_SOURCE_DIR}/externals/libchdr/src/libchdr_codec_avhuff.c)
target_include_directories(chdr PUBLIC ${CMAKE_SOURCE_DIR}/externals/libchdr/include)
target_include_directories(chdr PRIVATE ${CMAKE_SOURCE_DIR}/externals/libchdr)
target_compile_options(chdr PRIVATE -w)

target_link_libraries(ctr_native enet chdr)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    BUILD=926
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
target_compile_options(ctr_native PRIVATE
    -Wenum-conversion
    -Wenum-compare
    -Wswitch-enum
    -Wformat
    -Wparentheses
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-g>
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
    ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
endif()
target_link_libraries(ctr_native SDL3::SDL3)
# EGL: the dual-screen companion creates its own window surface against SDL's context.
target_link_libraries(ctr_native log GLESv2 EGL android)
