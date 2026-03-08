# kernel/CMakeLists.txt — Build system for the UNHOX kernel
#
# Supports two target architectures selected via the CMake toolchain file:
#   x86-64   — cmake/x86_64-elf-clang.cmake  (default)
#   AArch64  — cmake/aarch64-elf-clang.cmake
#
# Usage (from the repo root):
#
#   x86-64:
#     cmake -S kernel -B build \
#           -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
#           -DUNHOX_BOOT_TESTS=ON
#     cmake --build build
#
#   AArch64 (Apple Silicon / M-series Mac):
#     cmake -S kernel -B build-aarch64 \
#           -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-elf-clang.cmake \
#           -DUNHOX_BOOT_TESTS=ON
#     cmake --build build-aarch64
#
# Prerequisites (macOS arm64):
#   brew install llvm qemu

cmake_minimum_required(VERSION 3.20)

# When built standalone (cmake -S kernel -B build), define the project here.
# When built via the root CMakeLists.txt, the project is already defined.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(unhx_kernel C ASM)
endif()

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Optional: compile-in boot self-tests (Phase 1 milestone IPC test)
# ---------------------------------------------------------------------------
option(UNHOX_BOOT_TESTS "Include kernel boot self-tests" OFF)

# ---------------------------------------------------------------------------
# Freestanding kernel compile flags
# ---------------------------------------------------------------------------
# Flags common to all architectures:
#   -ffreestanding        : no standard library, no startup files
#   -fno-builtin          : do not use compiler built-in function replacements
#   -fno-stack-protector  : no stack canaries (no libc support)
#   -Wall/-Wextra         : enable broad warnings
#   -Werror               : treat warnings as errors for clean code

set(KERNEL_C_FLAGS_COMMON
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -Wall
    -Wextra
    -Wno-unused-function
    -Wno-unused-parameter
)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    # AArch64-specific flags — no x86 SIMD or model flags needed
    set(KERNEL_C_FLAGS ${KERNEL_C_FLAGS_COMMON})
    set(KERNEL_ASM_FLAGS -ffreestanding)
else()
    # x86-64-specific flags:
    #   -mno-red-zone         : disable the 128-byte red zone (unsafe in ISRs)
    #   -mno-mmx/-sse/-sse2   : no SIMD — we don't save/restore SIMD state yet
    set(KERNEL_C_FLAGS
        ${KERNEL_C_FLAGS_COMMON}
        -mno-red-zone
        -mno-mmx
        -mno-sse
        -mno-sse2
    )
    set(KERNEL_ASM_FLAGS -ffreestanding)
endif()

# ---------------------------------------------------------------------------
# Source files — architecture-specific platform layer + shared kernel core
# ---------------------------------------------------------------------------

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(PLATFORM_SOURCES
        platform/aarch64/boot.S
        platform/aarch64/context_switch.S
        platform/aarch64/platform.c
    )
    set(KERNEL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker-aarch64.ld)
else()
    set(PLATFORM_SOURCES
        # x86-64 platform layer
        platform/boot.S
        platform/context_switch.S
        platform/gdt.c
        platform/platform.c
        platform/paging.c
    )
    set(KERNEL_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
endif()

set(KERNEL_SOURCES
    ${PLATFORM_SOURCES}

    # IPC subsystem
    ipc/ipc.c
    ipc/ipc_mqueue.c
    ipc/ipc_kmsg.c
    ipc/ipc_right.c
    ipc/mach_msg.c

    # VM subsystem
    vm/vm.c
    vm/vm_page.c

    # Kernel core
    kern/kalloc.c
    kern/klib.c
    kern/kern.c
    kern/task.c
    kern/thread.c
    kern/sched.c
    kern/kernel_task.c

    # Bootstrap server (Phase 1: linked into kernel)
    ${CMAKE_CURRENT_SOURCE_DIR}/../servers/bootstrap/bootstrap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../servers/bootstrap/registry.c
)

if(UNHOX_BOOT_TESTS)
    list(APPEND KERNEL_SOURCES
        tests/ipc_test.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/ipc/ipc_roundtrip_test.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/ipc/ipc_perf.c
    )
    add_compile_definitions(UNHOX_BOOT_TESTS=1)
endif()

# ---------------------------------------------------------------------------
# Kernel binary target
# ---------------------------------------------------------------------------

add_executable(unhx.elf ${KERNEL_SOURCES})

target_include_directories(unhx.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../servers
    ${CMAKE_CURRENT_SOURCE_DIR}/../tests
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(unhx.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${KERNEL_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${KERNEL_ASM_FLAGS}>
)

# Link as a freestanding ELF with our custom linker script.
# When using the cross-compilation toolchain (cmake/x86_64-elf-clang.cmake or
# cmake/aarch64-elf-clang.cmake), ld.lld is invoked directly (not through
# clang), so these are ld.lld flags.
target_link_options(unhx.elf PRIVATE
    -T ${KERNEL_LINKER_SCRIPT}
    -z max-page-size=4096
    --static
)

set_target_properties(unhx.elf PROPERTIES
    LINK_DEPENDS ${KERNEL_LINKER_SCRIPT}
    SUFFIX ""                  # avoid CMake appending .elf twice
    OUTPUT_NAME "unhx.elf"
)

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS unhx.elf DESTINATION boot)
