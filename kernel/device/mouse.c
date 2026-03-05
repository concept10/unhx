/*
 * kernel/device/mouse.c — PS/2 mouse driver for UNHOX
 *
 * Enables the auxiliary PS/2 device on the i8042 controller and handles
 * IRQ12 interrupts. Parses standard 3-byte mouse packets into relative
 * motion deltas and button state.
 *
 * Reference: OSDev wiki "PS/2 Mouse", i8042 controller spec.
 */

#include "mouse.h"
#include "platform/ioport.h"
#include "platform/irq.h"
#include "platform/pic.h"

extern void serial_putstr(const char *s);

#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_CMD_PORT     0x64

#define IRQ_MOUSE        12

/* Wait for i8042 input buffer to be ready for writing */
static void mouse_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(KBD_STATUS_PORT) & 0x02))
            return;
    }
}

/* Wait for i8042 output buffer to have data for reading */
static void mouse_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(KBD_STATUS_PORT) & 0x01)
            return;
    }
}

/* Send a command byte to the i8042 controller */
static void mouse_cmd(uint8_t cmd)
{
    mouse_wait_write();
    outb(KBD_CMD_PORT, cmd);
}

/* Send a byte to the auxiliary PS/2 device (mouse) */
static void mouse_write(uint8_t data)
{
    mouse_wait_write();
    outb(KBD_CMD_PORT, 0xD4);   /* "Write to auxiliary device" */
    mouse_wait_write();
    outb(KBD_DATA_PORT, data);
}

/* Read a byte from the i8042 data port */
static uint8_t mouse_read(void)
{
    mouse_wait_read();
    return inb(KBD_DATA_PORT);
}

/* -------------------------------------------------------------------------
 * Packet state machine — 3-byte standard PS/2 mouse protocol
 * Byte 0: [yo|xo|ys|xs|1|mb|rb|lb]  (buttons + signs + overflow)
 * Byte 1: X movement (unsigned, sign in byte 0 bit 4)
 * Byte 2: Y movement (unsigned, sign in byte 0 bit 5)
 * ------------------------------------------------------------------------- */

static volatile uint8_t  packet_byte;   /* 0, 1, or 2 */
static volatile uint8_t  packet[3];

/* Accumulated state (read by mouse_read_event) */
static volatile int16_t  accum_dx;
static volatile int16_t  accum_dy;
static volatile uint8_t  cur_buttons;
static volatile int      has_activity;

static void mouse_irq_handler(struct interrupt_frame *frame)
{
    (void)frame;

    uint8_t status = inb(KBD_STATUS_PORT);
    if (!(status & 0x20))  /* Bit 5 = auxiliary output buffer full */
        return;

    uint8_t data = inb(KBD_DATA_PORT);

    switch (packet_byte) {
    case 0:
        /* Byte 0 must have bit 3 set (always-1 bit in PS/2 protocol) */
        if (!(data & 0x08))
            break;  /* Out of sync — wait for valid byte 0 */
        packet[0] = data;
        packet_byte = 1;
        break;
    case 1:
        packet[1] = data;
        packet_byte = 2;
        break;
    case 2:
        packet[2] = data;
        packet_byte = 0;

        /* Parse complete packet */
        cur_buttons = packet[0] & 0x07;

        int16_t dx = (int16_t)packet[1];
        int16_t dy = (int16_t)packet[2];

        /* Apply sign extension from byte 0 */
        if (packet[0] & 0x10) dx |= (int16_t)0xFF00;
        if (packet[0] & 0x20) dy |= (int16_t)0xFF00;

        /* Discard on overflow */
        if (!(packet[0] & 0xC0)) {
            accum_dx += dx;
            accum_dy += dy;
            has_activity = 1;
        }
        break;
    }
}

int mouse_read_event(mouse_event_t *out)
{
    uint64_t flags = irq_save();

    if (!has_activity) {
        out->dx = 0;
        out->dy = 0;
        out->buttons = cur_buttons;
        irq_restore(flags);
        return 0;
    }

    out->dx = accum_dx;
    out->dy = accum_dy;
    out->buttons = cur_buttons;

    accum_dx = 0;
    accum_dy = 0;
    has_activity = 0;

    irq_restore(flags);
    return 1;
}

void mouse_init(void)
{
    packet_byte = 0;
    accum_dx = 0;
    accum_dy = 0;
    cur_buttons = 0;
    has_activity = 0;

    /* Enable auxiliary device on the i8042 controller */

    /* Step 1: Enable the auxiliary port */
    mouse_cmd(0xA8);

    /* Step 2: Read controller config byte */
    mouse_cmd(0x20);
    uint8_t config = mouse_read();

    /* Step 3: Enable IRQ12 (bit 1) and auxiliary clock (clear bit 5) */
    config |= 0x02;     /* Enable IRQ12 */
    config &= ~0x20;    /* Enable auxiliary clock */

    /* Step 4: Write config back */
    mouse_cmd(0x60);
    mouse_wait_write();
    outb(KBD_DATA_PORT, config);

    /* Step 5: Send "Set Defaults" to mouse */
    mouse_write(0xF6);
    mouse_read();  /* ACK */

    /* Step 6: Enable data reporting */
    mouse_write(0xF4);
    mouse_read();  /* ACK */

    /* Register IRQ handler and unmask */
    irq_register(IRQ_MOUSE, mouse_irq_handler);
    pic_unmask(IRQ_MOUSE);

    serial_putstr("[mouse] initialized on IRQ12\r\n");
}
