/*
 * kernel/device/ahci.c — AHCI host controller probe for UNHOX
 *
 * This is an initial hardware-discovery implementation. It locates the
 * standard Intel ICH AHCI controller on QEMU q35 and prints HBA capability
 * and implemented-port information.
 */

#include "ahci.h"

#include "pci.h"

/* Serial diagnostics */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t v);
extern void serial_putdec(uint32_t v);

/* PCI class codes for AHCI */
#define PCI_CLASS_MASS_STORAGE      0x01
#define PCI_SUBCLASS_SATA           0x06
#define PCI_PROGIF_AHCI             0x01

/* AHCI HBA global register offsets */
#define AHCI_REG_CAP                0x00
#define AHCI_REG_GHC                0x04
#define AHCI_REG_PI                 0x0C

static int pci_is_ahci(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint32_t class_reg = pci_read_config(bus, slot, func, PCI_CONFIG_CLASS_CODE, 4);
    uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFF);
    uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFF);
    uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFF);

    return class_code == PCI_CLASS_MASS_STORAGE &&
           subclass == PCI_SUBCLASS_SATA &&
           prog_if == PCI_PROGIF_AHCI;
}

static volatile uint32_t *ahci_hba_from_bar5(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint32_t bar5 = pci_read_config(bus, slot, func, PCI_CONFIG_BAR5, 4);

    /* Require memory BAR; ignore I/O BARs for AHCI. */
    if (bar5 & 0x1)
        return (volatile uint32_t *)0;

    return (volatile uint32_t *)(uint64_t)(bar5 & ~0xFUL);
}

void ahci_init(void)
{
    int found = 0;

    serial_putstr("[ahci] probing controllers\r\n");

    for (uint8_t bus = 0; bus < 1; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor = pci_read_config(bus, slot, func, PCI_CONFIG_VENDOR_ID, 2);
                if ((vendor & 0xFFFF) == 0xFFFF) {
                    if (func == 0)
                        break;
                    continue;
                }

                if (!pci_is_ahci(bus, slot, func))
                    continue;

                found = 1;
                serial_putstr("[ahci] controller at ");
                serial_putdec(bus);
                serial_putstr(":");
                serial_putdec(slot);
                serial_putstr(".");
                serial_putdec(func);
                serial_putstr("\r\n");

                volatile uint32_t *hba = ahci_hba_from_bar5(bus, slot, func);
                if (!hba) {
                    serial_putstr("[ahci] WARN: invalid BAR5 for AHCI MMIO\r\n");
                    continue;
                }
                serial_putstr("[ahci] ABAR: ");
                serial_puthex((uint64_t)hba);
                serial_putstr("\r\n");
                serial_putstr("[ahci] MMIO probe deferred (ABAR not mapped in kernel VM yet)\r\n");
            }
        }
    }

    if (!found)
        serial_putstr("[ahci] no AHCI controller found\r\n");
}
