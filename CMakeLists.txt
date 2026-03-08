# CMakeLists.txt — Top-level build system for NEOMACH
#
# This is the repository-wide CMake entry point. It delegates to the kernel
# build and (in future phases) to userspace server builds.
#
# Usage:
#   cmake -S . -B build \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
#         -DNEOMACH_BOOT_TESTS=ON
#   cmake --build build

cmake_minimum_required(VERSION 3.20)
project(neomach LANGUAGES C ASM)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(NEOMACH_BOOT_TESTS "Include kernel boot self-tests" OFF)

# ---------------------------------------------------------------------------
# Kernel (always built)
# ---------------------------------------------------------------------------
add_subdirectory(kernel)

# ---------------------------------------------------------------------------
# Userspace servers (Phase 2+)
# ---------------------------------------------------------------------------
# add_subdirectory(servers/bootstrap)
# add_subdirectory(servers/bsd)
# add_subdirectory(servers/vfs)
# add_subdirectory(servers/device)
# add_subdirectory(servers/network)
# add_subdirectory(servers/auth)
