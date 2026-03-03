/*
 * kernel/device/keyboard.h — i8042 keyboard driver for UNHOX
 *
 * Provides IRQ-driven keyboard input via the legacy PS/2 controller.
 * This gives immediate interactive input support while USB HID stack work
 * is in progress.
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Initialize keyboard IRQ routing and controller state. */
void keyboard_init(void);

/* Non-blocking read: returns ASCII char, or 0 if no key is available. */
char keyboard_getchar_nonblock(void);

/* Emit a short self-test message and current buffer status. */
void keyboard_test(void);

#endif /* KEYBOARD_H */
