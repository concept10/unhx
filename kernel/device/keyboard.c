/*
 * kernel/device/keyboard.c — i8042 PS/2 keyboard driver for UNHOX
 *
 * IRQ1 handler reads scan set 1 bytes from port 0x60, translates a
 * practical subset to ASCII, and stores characters in a small ring buffer.
 */

#include "keyboard.h"

#include "platform/ioport.h"
#include "platform/irq.h"
#include "platform/pic.h"

/* Serial diagnostics */
extern void serial_putstr(const char *s);
extern void serial_putchar(char c);

#define KBD_DATA_PORT       0x60
#define KBD_STATUS_PORT     0x64
#define KBD_STATUS_OBF      0x01
#define KBD_STATUS_IBF      0x02

#define KBD_BUF_SIZE        128

static volatile uint8_t kbd_shift;
static volatile uint8_t kbd_caps;
static volatile uint8_t kbd_initialized;

static volatile uint8_t kbd_head;
static volatile uint8_t kbd_tail;
static char kbd_buf[KBD_BUF_SIZE];

static const char scancode_to_ascii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', /* 0x00-0x09 */
    '9', '0', '-', '=', '\b','\t','q', 'w', 'e', 'r', /* 0x0A-0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',0,   /* 0x14-0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 0x1E-0x27 */
    '\'', '`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n', /* 0x28-0x31 */
    'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   /* 0x32-0x3B */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.', 0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0
};

static const char scancode_to_ascii_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b','\t','Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   '7', '8', '9', '-', '4', '5', '6', '+',
    '1', '2', '3', '0', '.', 0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0
};

static inline int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static void kbd_buf_push(char c)
{
    uint8_t next = (uint8_t)((kbd_head + 1) % KBD_BUF_SIZE);
    if (next == kbd_tail) {
        /* Drop oldest char on overflow. */
        kbd_tail = (uint8_t)((kbd_tail + 1) % KBD_BUF_SIZE);
    }
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static char kbd_translate_make(uint8_t sc)
{
    char c;
    int apply_shift = kbd_shift;

    if (sc >= 128)
        return 0;

    c = apply_shift ? scancode_to_ascii_shift[sc] : scancode_to_ascii[sc];
    if (!c)
        return 0;

    if (kbd_caps && is_alpha(c)) {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - ('a' - 'A'));
        else if (c >= 'A' && c <= 'Z')
            c = (char)(c + ('a' - 'A'));
    }

    return c;
}

static void keyboard_irq_handler(struct interrupt_frame *frame)
{
    (void)frame;

    while (inb(KBD_STATUS_PORT) & KBD_STATUS_OBF) {
        uint8_t sc = inb(KBD_DATA_PORT);
        uint8_t release = (uint8_t)(sc & 0x80);
        uint8_t code = (uint8_t)(sc & 0x7F);
        char c;

        /* Track modifier state. */
        if (code == 0x2A || code == 0x36) {
            kbd_shift = release ? 0 : 1;
            continue;
        }
        if (!release && code == 0x3A) {
            kbd_caps ^= 1;
            continue;
        }

        if (release)
            continue;

        c = kbd_translate_make(code);
        if (c)
            kbd_buf_push(c);
    }
}

void keyboard_init(void)
{
    if (kbd_initialized)
        return;

    /* Drain stale output bytes before enabling IRQ handling. */
    while (inb(KBD_STATUS_PORT) & KBD_STATUS_OBF)
        (void)inb(KBD_DATA_PORT);

    kbd_shift = 0;
    kbd_caps = 0;
    kbd_head = 0;
    kbd_tail = 0;

    irq_register(IRQ_KEYBOARD, keyboard_irq_handler);
    pic_unmask(IRQ_KEYBOARD);

    kbd_initialized = 1;
    serial_putstr("[keyboard] initialized on IRQ1\r\n");
}

char keyboard_getchar_nonblock(void)
{
    uint64_t flags;
    char c;

    if (!kbd_initialized || kbd_head == kbd_tail)
        return 0;

    flags = irq_save();
    if (kbd_head == kbd_tail) {
        irq_restore(flags);
        return 0;
    }

    c = kbd_buf[kbd_tail];
    kbd_tail = (uint8_t)((kbd_tail + 1) % KBD_BUF_SIZE);
    irq_restore(flags);

    return c;
}

void keyboard_test(void)
{
    serial_putstr("[keyboard] test: type keys in QEMU window\r\n");
    serial_putstr("[keyboard] buffered echo enabled\r\n");
}
