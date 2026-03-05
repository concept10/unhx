# frameworks/CMakeLists.txt — UNHOX framework layer build
#
# Builds: unhx_objc, unhx_foundation, unhx_dispatch as static libraries.
# Note: frameworks/libdispatch/ has an upstream CMakeLists.txt (submodule),
# so we build our dispatch sources directly here instead of add_subdirectory.

# --- ObjC Runtime ---
add_subdirectory(objc-runtime)

# --- Foundation ---
add_subdirectory(Foundation)

# --- libdispatch (our Mach-native implementation, not the upstream submodule) ---
add_library(unhx_dispatch STATIC
    libdispatch/blocks_runtime.c
    libdispatch/dispatch_queue.c
    libdispatch/dispatch_group.c
)

target_compile_options(unhx_dispatch PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${USER_C_FLAGS}>
)

target_include_directories(unhx_dispatch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libdispatch
    ${CMAKE_CURRENT_SOURCE_DIR}/objc-runtime
)

target_link_libraries(unhx_dispatch PUBLIC unhx_libc)
