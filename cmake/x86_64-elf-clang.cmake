# cmake/x86_64-elf-clang.cmake — Cross-compilation toolchain for UNHOX
#
# Builds an x86-64 freestanding ELF kernel from any host (including arm64 macOS).
#
# Uses clang with --target=x86_64-unknown-elf for compilation and assembly,
# and LLVM's ld.lld directly for ELF linking (bypassing the clang driver
# to avoid macOS-specific linker flags being injected).
#
# Prerequisites:
#   macOS:  brew install llvm lld qemu
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
# Detect LLVM clang (prefer brew LLVM, fallback to system clang)
# ---------------------------------------------------------------------------
if(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
    set(LLVM_CLANG "/opt/homebrew/opt/llvm/bin/clang")
elseif(EXISTS "/usr/local/opt/llvm/bin/clang")
    set(LLVM_CLANG "/usr/local/opt/llvm/bin/clang")
else()
    set(LLVM_CLANG "clang")
endif()

# ---------------------------------------------------------------------------
# Detect ld.lld (brew lld installs to /opt/homebrew/bin/ld.lld)
# ---------------------------------------------------------------------------
if(EXISTS "/opt/homebrew/bin/ld.lld")
    set(LLD_LINKER "/opt/homebrew/bin/ld.lld")
elseif(EXISTS "/usr/local/bin/ld.lld")
    set(LLD_LINKER "/usr/local/bin/ld.lld")
else()
    find_program(LLD_LINKER ld.lld REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Detect llvm-ar (needed for creating static libraries with ELF objects)
# ---------------------------------------------------------------------------
if(EXISTS "/opt/homebrew/opt/llvm/bin/llvm-ar")
    set(LLVM_AR "/opt/homebrew/opt/llvm/bin/llvm-ar")
elseif(EXISTS "/usr/local/opt/llvm/bin/llvm-ar")
    set(LLVM_AR "/usr/local/opt/llvm/bin/llvm-ar")
else()
    find_program(LLVM_AR llvm-ar REQUIRED)
endif()

if(EXISTS "/opt/homebrew/opt/llvm/bin/llvm-ranlib")
    set(LLVM_RANLIB "/opt/homebrew/opt/llvm/bin/llvm-ranlib")
elseif(EXISTS "/usr/local/opt/llvm/bin/llvm-ranlib")
    set(LLVM_RANLIB "/usr/local/opt/llvm/bin/llvm-ranlib")
else()
    find_program(LLVM_RANLIB llvm-ranlib REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Set compilers
# ---------------------------------------------------------------------------
set(CMAKE_C_COMPILER   "${LLVM_CLANG}")
set(CMAKE_ASM_COMPILER "${LLVM_CLANG}")
set(CMAKE_LINKER       "${LLD_LINKER}")
set(CMAKE_AR           "${LLVM_AR}")
set(CMAKE_RANLIB       "${LLVM_RANLIB}")

set(CMAKE_C_COMPILER_TARGET   "x86_64-unknown-elf")
set(CMAKE_ASM_COMPILER_TARGET "x86_64-unknown-elf")

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
# Tell CMake this is a freestanding environment — skip standard library checks
set(CMAKE_C_COMPILER_WORKS   TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT   "--target=x86_64-unknown-elf")
set(CMAKE_ASM_FLAGS_INIT "--target=x86_64-unknown-elf")

# ---------------------------------------------------------------------------
# Override the link command to invoke ld.lld directly
# ---------------------------------------------------------------------------
# CMake's default link command uses the C compiler as the linker driver,
# which on macOS causes Apple-specific flags (-arch, -platform_version,
# -syslibroot) to be injected even with --target=x86_64-unknown-elf.
#
# We bypass this entirely by invoking ld.lld directly.
# <CMAKE_LINKER> = ld.lld path
# <FLAGS>        = target_link_options from CMakeLists.txt
# <OBJECTS>      = compiled .obj files
# <TARGET>       = output filename
# <LINK_FLAGS>   = additional link flags
set(CMAKE_C_LINK_EXECUTABLE
    "${LLD_LINKER} <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
)

# Don't search for host programs/libraries (cross-compile mode)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
