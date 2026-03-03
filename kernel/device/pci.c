/*
 * kernel/device/pci.c — PCI device enumeration implementation
 */

#include "pci.h"
#include "kern/klib.h"
#include "platform/ioport.h"

extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);

/* Global PCI device array */
#define PCI_MAX_DEVICES 32
static struct pci_device pci_devices[PCI_MAX_DEVICES];
static uint32_t pci_device_count = 0;

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t function,
                         uint8_t offset, uint8_t size)
{
    uint32_t address = 0x80000000UL;
    address |= ((uint32_t)bus << 16);
    address |= ((uint32_t)slot << 11);
    address |= ((uint32_t)function << 8);
    address |= (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS, address);
    
    uint32_t data = inl(PCI_CONFIG_DATA);
    
    /* Shift and mask based on size and offset alignment */
    uint32_t shift = (offset & 0x03) * 8;
    if (size == 1)
        return (data >> shift) & 0xFF;
    else if (size == 2)
        return (data >> shift) & 0xFFFF;
    else
        return data;
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t function,
                      uint8_t offset, uint8_t size, uint32_t value)
{
    uint32_t address = 0x80000000UL;
    address |= ((uint32_t)bus << 16);
    address |= ((uint32_t)slot << 11);
    address |= ((uint32_t)function << 8);
    address |= (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS, address);
    
    if (size == 1) {
        uint32_t shift = (offset & 0x03) * 8;
        uint32_t mask = 0xFFFFFFFFUL ^ (0xFF << shift);
        uint32_t data = inl(PCI_CONFIG_DATA);
        data = (data & mask) | ((value & 0xFF) << shift);
        outl(PCI_CONFIG_DATA, data);
    } else if (size == 2) {
        uint32_t shift = (offset & 0x02) * 8;
        uint32_t mask = 0xFFFFFFFFUL ^ (0xFFFF << shift);
        uint32_t data = inl(PCI_CONFIG_DATA);
        data = (data & mask) | ((value & 0xFFFF) << shift);
        outl(PCI_CONFIG_DATA, data);
    } else {
        outl(PCI_CONFIG_DATA, value);
    }
}

void pci_enable_bus_master(struct pci_device *dev)
{
    uint32_t cmd = pci_read_config(dev->bus, dev->slot, dev->function,
                                   PCI_CONFIG_COMMAND, 2);
    cmd |= 0x04;  /* Bus Master Enable */
    pci_write_config(dev->bus, dev->slot, dev->function,
                     PCI_CONFIG_COMMAND, 2, cmd);
}

static void pci_read_device(uint8_t bus, uint8_t slot, uint8_t function)
{
    uint16_t vendor_id = pci_read_config(bus, slot, function, PCI_CONFIG_VENDOR_ID, 2);
    
    /* Vendor ID 0xFFFF means device not present */
    if (vendor_id == 0xFFFF)
        return;
    
    if (pci_device_count >= PCI_MAX_DEVICES)
        return;
    
    struct pci_device *dev = &pci_devices[pci_device_count++];
    
    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_read_config(bus, slot, function, PCI_CONFIG_DEVICE_ID, 2);
    dev->class_code = pci_read_config(bus, slot, function, PCI_CONFIG_CLASS_CODE, 1);
    dev->subclass = pci_read_config(bus, slot, function, PCI_CONFIG_CLASS_CODE + 1, 1);
    dev->prog_if = pci_read_config(bus, slot, function, PCI_CONFIG_CLASS_CODE + 2, 1);
    dev->interrupt_line = pci_read_config(bus, slot, function, PCI_CONFIG_INTERRUPT_LINE, 1);
    dev->interrupt_pin = pci_read_config(bus, slot, function, PCI_CONFIG_INTERRUPT_PIN, 1);
    dev->driver_data = (void *)0;
    
    /* Read BAR0–BAR5 */
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_config(bus, slot, function,
                                      PCI_CONFIG_BAR0 + i * 4, 4);
    }
    
    serial_putstr("[pci] device ");
    serial_puthex(vendor_id);
    serial_putstr(":");
    serial_puthex(dev->device_id);
    serial_putstr(" at ");
    serial_puthex(bus);
    serial_putstr(":");
    serial_puthex(slot);
    serial_putstr(".");
    serial_puthex(function);
    serial_putstr("\r\n");
}

void pci_init(void)
{
    serial_putstr("[pci] enumerating devices\r\n");
    
    /* Enumerate all devices on bus 0 (for now, single-bus only) */
    for (uint32_t slot = 0; slot < 32; slot++) {
        for (uint32_t function = 0; function < 8; function++) {
            pci_read_device(0, slot, function);
        }
    }
    
    serial_putstr("[pci] found ");
    serial_puthex(pci_device_count);
    serial_putstr(" devices\r\n");
}

struct pci_device *pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    for (uint32_t i = 0; i < pci_device_count; i++) {
        struct pci_device *dev = &pci_devices[i];
        if (dev->vendor_id == vendor_id && dev->device_id == device_id)
            return dev;
    }
    return (void *)0;
}
