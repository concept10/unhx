/*
 * kernel/device/mouse.h — PS/2 mouse driver for UNHOX
 *
 * Provides IRQ12-driven mouse input via the i8042 auxiliary port.
 * Reports relative motion (dx, dy) and button state.
 */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

/* Mouse button bits */
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

/* Mouse event — accumulated since last read */
typedef struct {
    int16_t  dx;        /* relative X movement */
    int16_t  dy;        /* relative Y movement */
    uint8_t  buttons;   /* MOUSE_BTN_* bitmask */
} mouse_event_t;

/* Initialize PS/2 mouse via i8042 controller, register IRQ12. */
void mouse_init(void);

/*
 * mouse_read_event — non-blocking read of accumulated mouse movement.
 * Returns 1 if there was activity since last call, 0 if no movement.
 * Resets the accumulators after reading.
 */
int mouse_read_event(mouse_event_t *out);

#endif /* MOUSE_H */
