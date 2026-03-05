# user/libc/CMakeLists.txt — UNHOX userspace C library (static)

add_library(unhx_libc STATIC
    malloc.c
    string.c
    stdio.c
    mach_msg.c
    thread.c
)

target_compile_options(unhx_libc PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${USER_C_FLAGS}>
)

target_include_directories(unhx_libc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
