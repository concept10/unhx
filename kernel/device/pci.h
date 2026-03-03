/*
 * kernel/device/pci.h — PCI device enumeration and configuration
 *
 * Minimal PCI host bridge and device enumeration for QEMU q35.
 * Provides device discovery, BAR mapping, and interrupt routing.
 *
 * Reference: PCI Local Bus Specification 3.0, Chapter 6
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* PCI Configuration Space Access (via I/O ports) */
#define PCI_CONFIG_ADDRESS     0xCF8
#define PCI_CONFIG_DATA        0xCFC

/* PCI Configuration Register Offsets */
#define PCI_CONFIG_VENDOR_ID       0x00  /* 2 bytes */
#define PCI_CONFIG_DEVICE_ID       0x02  /* 2 bytes */
#define PCI_CONFIG_COMMAND        0x04  /* 2 bytes */
#define PCI_CONFIG_STATUS         0x06  /* 2 bytes */
#define PCI_CONFIG_REVISION       0x08  /* 1 byte */
#define PCI_CONFIG_CLASS_CODE     0x09  /* 3 bytes */
#define PCI_CONFIG_CACHE_LINE     0x0C  /* 1 byte */
#define PCI_CONFIG_HEADER_TYPE    0x0E  /* 1 byte */
#define PCI_CONFIG_BAR0           0x10  /* 4 bytes each */
#define PCI_CONFIG_BAR1           0x14
#define PCI_CONFIG_BAR2           0x18
#define PCI_CONFIG_BAR3           0x1C
#define PCI_CONFIG_BAR4           0x20
#define PCI_CONFIG_BAR5           0x24
#define PCI_CONFIG_INTERRUPT_PIN  0x3D  /* 1 byte */
#define PCI_CONFIG_INTERRUPT_LINE 0x3C  /* 1 byte */

/* PCI Device Classes */
#define PCI_CLASS_STORAGE         0x01
#define PCI_CLASS_NETWORK         0x02
#define PCI_CLASS_INPUT           0x09
#define PCI_CLASS_PROCESSOR       0x0B

/* PCI Storage Subclasses */
#define PCI_STORAGE_SCSI          0x00
#define PCI_STORAGE_IDE           0x01
#define PCI_STORAGE_FLOPPY        0x02
#define PCI_STORAGE_IPI           0x03
#define PCI_STORAGE_RAID          0x04
#define PCI_STORAGE_ATA           0x05  /* SATA */

/* Structure to hold PCI device information */
struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    
    uint32_t bar[6];           /* Base Address Registers */
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    
    /* Device-specific state */
    void *driver_data;
};

/* Virtio vendor ID (unique across all vendors) */
#define VIRTIO_VENDOR_ID      0x1AF4

/* Virtio device IDs */
#define VIRTIO_DEVICE_NET     0x1000
#define VIRTIO_DEVICE_BLOCK   0x1001
#define VIRTIO_DEVICE_CONSOLE 0x1003
#define VIRTIO_DEVICE_ENTROPY 0x1004
#define VIRTIO_DEVICE_9P      0x1009

/* PCI device discovery and enumeration */
void pci_init(void);

/*
 * pci_find_device — search for a PCI device by vendor and device ID.
 *
 * Returns a pointer to the matching device, or NULL if not found.
 * The returned structure is statically allocated; do not free.
 */
struct pci_device *pci_find_device(uint16_t vendor_id, uint16_t device_id);

/*
 * pci_read_config — read a configuration space register.
 *
 * bus, slot, function — PCI address
 * offset — register offset (0–255)
 * size — 1, 2, or 4 bytes
 *
 * Returns the value read.
 */
uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t function,
                         uint8_t offset, uint8_t size);

/*
 * pci_write_config — write a configuration space register.
 */
void pci_write_config(uint8_t bus, uint8_t slot, uint8_t function,
                      uint8_t offset, uint8_t size, uint32_t value);

/*
 * pci_enable_bus_master — enable DMA by setting PCI command bit 2.
 */
void pci_enable_bus_master(struct pci_device *dev);

#endif /* PCI_H */
