# user/CMakeLists.txt — Build system for UNHOX userspace programs

cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(USER_C_FLAGS
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -mno-red-zone
    -mno-mmx
    -mno-sse
    -mno-sse2
    -Wall
    -Wextra
    -Wno-unused-parameter
)

set(USER_ASM_FLAGS
    -ffreestanding
)

# ---------------------------------------------------------------------------
# unhx_libc — minimal C library (static)
# ---------------------------------------------------------------------------
add_subdirectory(libc)

# ---------------------------------------------------------------------------
# init.elf — the first userspace process
# ---------------------------------------------------------------------------

add_executable(init.elf
    libc/start.S
    init/init.c
    sh/sh.c
)

target_compile_options(init.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${USER_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${USER_ASM_FLAGS}>
)

target_include_directories(init.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libc
)

target_link_options(init.elf PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -z max-page-size=4096
    --static
)

set_target_properties(init.elf PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    SUFFIX ""
    OUTPUT_NAME "init.elf"
)

# ---------------------------------------------------------------------------
# test_objc.elf — ObjC runtime test program (Phase 4 milestone v0.7)
# ---------------------------------------------------------------------------

add_executable(test_objc.elf
    libc/start.S
    tests/test_objc.c
)

target_compile_options(test_objc.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${USER_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${USER_ASM_FLAGS}>
)

target_include_directories(test_objc.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libc
)

target_link_libraries(test_objc.elf PRIVATE
    unhx_foundation
    unhx_objc
    unhx_libc
)

target_link_options(test_objc.elf PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -z max-page-size=4096
    --static
)

set_target_properties(test_objc.elf PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    SUFFIX ""
    OUTPUT_NAME "test_objc.elf"
)

# ---------------------------------------------------------------------------
# test_dispatch.elf — libdispatch test program (Phase 4 dispatch_async)
# ---------------------------------------------------------------------------

add_executable(test_dispatch.elf
    libc/start.S
    tests/test_dispatch.c
)

target_compile_options(test_dispatch.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${USER_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${USER_ASM_FLAGS}>
)

target_include_directories(test_dispatch.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libc
)

target_link_libraries(test_dispatch.elf PRIVATE
    unhx_dispatch
    unhx_foundation
    unhx_objc
    unhx_libc
)

target_link_options(test_dispatch.elf PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -z max-page-size=4096
    --static
)

set_target_properties(test_dispatch.elf PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    SUFFIX ""
    OUTPUT_NAME "test_dispatch.elf"
)
