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
# init.elf — the first userspace process
# ---------------------------------------------------------------------------

add_executable(init.elf
    libc/start.S
    init/init.c
)

target_compile_options(init.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${USER_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${USER_ASM_FLAGS}>
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
