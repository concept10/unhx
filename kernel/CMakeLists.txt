# kernel/CMakeLists.txt — Build system for the UNHOX kernel
#
# Configures a freestanding x86-64 ELF kernel binary.
# Requires an x86_64-elf cross-compiler (or clang with --target=x86_64-elf).
#
# Usage (from the repo root):
#   cmake -S kernel -B build \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
#         -DUNHOX_BOOT_TESTS=ON
#   cmake --build build
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
# -ffreestanding        : no standard library, no startup files
# -mno-red-zone         : disable the 128-byte red zone (unsafe in kernel ISRs)
# -mno-mmx/-sse/-sse2   : no SIMD — we do not save/restore SIMD state on
#                         context switch in Phase 1
# -mcmodel=kernel       : generate code that assumes the kernel is in the
#                         upper 2 GB of the address space (matches VIRT_BASE)
# -Wall/-Wextra         : enable broad warnings
# -Werror               : treat warnings as errors for clean code

set(KERNEL_C_FLAGS
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -mno-red-zone
    -mno-mmx
    -mno-sse
    -mno-sse2
    -Wall
    -Wextra
    -Wno-unused-function
    -Wno-unused-parameter
)

set(KERNEL_ASM_FLAGS
    -ffreestanding
)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------

set(KERNEL_SOURCES
    # Platform layer (boot and hardware abstraction)
    platform/boot.S
    platform/context_switch.S
    platform/isr.S
    platform/gdt.c
    platform/platform.c
    platform/paging.c
    platform/idt.c
    platform/pic.c
    platform/irq.c

    # IPC subsystem
    ipc/ipc.c
    ipc/ipc_mqueue.c
    ipc/ipc_kmsg.c

    # VM subsystem
    vm/vm.c
    vm/vm_page.c
    vm/vm_map.c

    # Kernel core
    kern/kalloc.c
    kern/zalloc.c
    kern/klib.c
    kern/kern.c
    kern/task.c
    kern/thread.c
    kern/sched.c
    kern/kernel_task.c
    kern/elf_load.c

    # Bootstrap server (Phase 1: linked into kernel)
    ${CMAKE_CURRENT_SOURCE_DIR}/../servers/bootstrap/bootstrap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../servers/bootstrap/registry.c
)

if(UNHOX_BOOT_TESTS)
    list(APPEND KERNEL_SOURCES
        tests/ipc_test.c
        tests/ipc_perf.c
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
)

target_compile_options(unhx.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${KERNEL_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:ASM>:${KERNEL_ASM_FLAGS}>
)

# Link as a freestanding ELF with our custom linker script.
# When using the cross-compilation toolchain (cmake/x86_64-elf-clang.cmake),
# ld.lld is invoked directly (not through clang), so these are ld.lld flags.
target_link_options(unhx.elf PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -z max-page-size=4096
    --static
)

set_target_properties(unhx.elf PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    SUFFIX ""                  # avoid CMake appending .elf twice
    OUTPUT_NAME "unhx.elf"
)

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS unhx.elf DESTINATION boot)
