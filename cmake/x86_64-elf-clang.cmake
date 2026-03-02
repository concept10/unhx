# cmake/x86_64-elf-clang.cmake — Cross-compilation toolchain for UNHOX
#
# Builds an x86-64 freestanding ELF kernel from any host (including arm64 macOS).
#
# Uses clang with --target=x86_64-unknown-elf for compilation and assembly,
# and LLVM's lld (ld.lld) for ELF linking.
#
# Prerequisites:
#   macOS:  brew install llvm qemu
#   Linux:  apt install clang lld qemu-system-x86 (or equivalent)
#
# Usage:
#   cmake -S kernel -B build \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake
#   cmake --build build

cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ---------------------------------------------------------------------------
# Detect LLVM installation
# ---------------------------------------------------------------------------
# On macOS with Homebrew, LLVM is at /opt/homebrew/opt/llvm (Apple Silicon)
# or /usr/local/opt/llvm (Intel Mac).

if(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
    set(LLVM_PREFIX "/opt/homebrew/opt/llvm")
elseif(EXISTS "/usr/local/opt/llvm/bin/clang")
    set(LLVM_PREFIX "/usr/local/opt/llvm")
elseif(EXISTS "/usr/bin/clang")
    set(LLVM_PREFIX "")
endif()

# ---------------------------------------------------------------------------
# Compilers
# ---------------------------------------------------------------------------
if(LLVM_PREFIX)
    set(CMAKE_C_COMPILER   "${LLVM_PREFIX}/bin/clang")
    set(CMAKE_ASM_COMPILER "${LLVM_PREFIX}/bin/clang")
    set(CMAKE_LINKER       "${LLVM_PREFIX}/bin/ld.lld")
else()
    # Fallback to system clang (may work on Linux; on macOS needs lld in PATH)
    set(CMAKE_C_COMPILER   "clang")
    set(CMAKE_ASM_COMPILER "clang")
    find_program(LLD_LINKER ld.lld REQUIRED)
    set(CMAKE_LINKER       "${LLD_LINKER}")
endif()

set(CMAKE_C_COMPILER_TARGET   "x86_64-unknown-elf")
set(CMAKE_ASM_COMPILER_TARGET "x86_64-unknown-elf")

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
# Tell CMake this is a freestanding environment — skip standard library checks
set(CMAKE_C_COMPILER_WORKS   TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Use lld as the linker (produces ELF output, unlike Apple's ld64)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld --target=x86_64-unknown-elf")
set(CMAKE_C_FLAGS_INIT          "--target=x86_64-unknown-elf")
set(CMAKE_ASM_FLAGS_INIT        "--target=x86_64-unknown-elf")

# Don't search for host programs/libraries (cross-compile mode)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
