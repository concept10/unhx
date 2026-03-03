/*
 * kernel/device/ahci.h — AHCI host controller driver for UNHOX
 *
 * AHCI (Advanced Host Controller Interface) is a standardized interface for
 * SATA controllers. This driver:
 * 1. Discovers AHCI controllers on PCI
 * 2. Maps the ABAR (base address register) into kernel VM
 * 3. Initializes the command engine for one port
 * 4. Provides submit_command() for basic I/O operations
 *
 * Reference: SATA Revision 3.3 Advanced Host Controller Interface Specification.
 */

#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

/* ----- AHCI HBA Register Layout ----- */

/* HBA Memory Space offsets (all 32-bit registers) */
#define AHCI_REG_CAP        0x00        /* Host Capabilities */
#define AHCI_REG_GHC        0x04        /* Global HBA Control */
#define AHCI_REG_ISR        0x08        /* Interrupt Status Register */
#define AHCI_REG_PI         0x0C        /* Ports Implemented */
#define AHCI_REG_AHCI_VS    0x10        /* AHCI Version */
#define AHCI_REG_CCC_CTL    0x14        /* Command Completion Coalescing Control */
#define AHCI_REG_CCC_PORTS  0x18        /* Command Completion Coalescing Ports */
#define AHCI_REG_EM_LOC     0x1C        /* Enclosure Management Location */
#define AHCI_REG_EM_CTL     0x20        /* Enclosure Management Control */
#define AHCI_REG_CAP2       0x24        /* Host Capabilities Extended */
#define AHCI_REG_BOHC       0x28        /* BIOS/OS Handoff Control */

/* Per-port offsets (port 0 at 0x100, port 1 at 0x180, etc.) */
#define AHCI_PORT_BASE(n)   (0x100 + (n) * 0x80)

/* Port Register offsets (relative to AHCI_PORT_BASE) */
#define AHCI_PORT_CLB       0x00        /* Command List Base Address (32-bit) */
#define AHCI_PORT_CLBU      0x04        /* Command List Base Address Upper (32-bit) */
#define AHCI_PORT_FB        0x08        /* FIS Base Address (32-bit) */
#define AHCI_PORT_FBU       0x0C        /* FIS Base Address Upper (32-bit) */
#define AHCI_PORT_IS        0x10        /* Interrupt Status */
#define AHCI_PORT_IE        0x14        /* Interrupt Enable */
#define AHCI_PORT_CMD       0x18        /* Command and Status */
#define AHCI_PORT_TFD       0x20        /* Task File Data */
#define AHCI_PORT_SIG       0x24        /* Signature */
#define AHCI_PORT_SST       0x28        /* Serial ATA Status */
#define AHCI_PORT_SCR_CTL   0x2C        /* SATA Control */
#define AHCI_PORT_SCR_STAT  0x30        /* SATA Status */
#define AHCI_PORT_SCR_ERR   0x34        /* SATA Error */
#define AHCI_PORT_SACT      0x34        /* SATA Active */
#define AHCI_PORT_CI        0x38        /* Command Issue */
#define AHCI_PORT_SNTF      0x3C        /* SATA Notification */

/* GHC flags */
#define GHC_HR              0x00000001  /* HBA Reset */
#define GHC_IE              0x00000002  /* Interrupt Enable */
#define GHC_AE              0x80000000  /* AHCI Enable */

/* CMD flags */
#define CMD_ST              0x00000001  /* Start */
#define CMD_FRE             0x00000010  /* FIS Receive Enable */
#define CMD_FR              0x00004000  /* FIS Receive Running */
#define CMD_CR              0x00008000  /* Command List Running */

/* ----- Command Structure ----- */

/* Command Header: 32 bytes per command */
struct ahci_cmd_hdr {
    uint16_t flags;             /* Command FIS length, PM port, direction */
    uint16_t prdtl;             /* Physical Region Descriptor Table Length */
    uint32_t prdbc;             /* Physical Region Descriptor Byte Count */
    uint32_t ctba;              /* Command Table Descriptor Base Address */
    uint32_t ctbau;             /* Command Table Descriptor Base Address Upper */
    uint32_t reserved[4];
};

/* Command Table: contains FIS and PRD table */
#define AHCI_MAX_PRDT_ENTRIES   248

struct ahci_cmd_tbl {
    uint8_t cfis[64];           /* Command FIS (Register H2D) */
    uint8_t acmd[16];           /* ATAPI Command */
    uint8_t reserved[48];
    /* PRD entries follow at offset 128 */
    struct {
        uint32_t dba;           /* Data Base Address */
        uint32_t dbau;          /* Data Base Address Upper */
        uint32_t reserved;
        uint32_t dbc;           /* Data Byte Count (bits [21:0]) */
    } prdt[AHCI_MAX_PRDT_ENTRIES];
};

/* Received FIS Structure: 256 bytes per port */
struct ahci_received_fis {
    uint8_t dsfis[28];          /* Device to Host Register FIS */
    uint8_t pio_reserved[4];
    uint8_t psfis[20];          /* PIO Setup FIS */
    uint8_t pio_setup_reserved[12];
    uint8_t rfis[8];            /* Set Device Bits FIS */
    uint8_t rfis_reserved[4];
    uint8_t sdbfis[8];          /* SDB FIS */
    uint8_t sdb_reserved[56];
    uint8_t ufis[64];           /* Unknown FIS */
    uint8_t reserved[96];
};

/* Initialize AHCI controllers and ports. Called during kernel boot. */
void ahci_init(void);

/* Test function: verify basic command engine setup. */
void ahci_test(void);

#endif /* AHCI_H */
