/*
 * user/libc/start.S — User-mode entry point for UNHOX programs
 *
 * The kernel starts user threads at _start.
 * _start calls main(), then calls exit() with the return value.
 */

    .section .text
    .globl _start
    .type  _start, @function

_start:
    /* Call main() */
    call    main

    /* main() returned — call exit(return_value) */
    movq    %rax, %rdi      /* exit status = main() return value */
    movq    $2, %rax        /* SYS_EXIT = 2 */
    int     $0x80

    /* Should not reach here */
.Lhalt:
    jmp     .Lhalt
