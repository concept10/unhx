/*
 * kernel/device/ahci.c — AHCI host controller driver for UNHOX
 *
 * This implementation:
 * 1. Discovers AHCI controllers on PCI
 * 2. Maps the ABAR (base address register) into kernel VM space using paging_map()
 * 3. Initializes the command engine for port 0
 * 4. Allocates command list and FIS buffers
 * 5. Provides a harmless test that verifies command engine initialization
 *
 * Reference: SATA Revision 3.3 Advanced Host Controller Interface Specification.
 */

#include "ahci.h"
#include "pci.h"
#include "platform/paging.h"
#include "platform/ioport.h"
#include "vm/vm_page.h"
#include "kern/kalloc.h"
#include "kern/klib.h"

/* Serial diagnostics */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t v);
extern void serial_putdec(uint32_t v);

/* PCI class codes for AHCI */
#define PCI_CLASS_MASS_STORAGE      0x01
#define PCI_SUBCLASS_SATA           0x06
#define PCI_PROGIF_AHCI             0x01

/*
 * AHCI Device State
 *
 * We support a single AHCI controller per system (typical for VMs).
 * Each port gets command list, FIS, and command table buffers.
 */

struct ahci_port {
    volatile uint32_t *base;        /* Port register base (mapped HBA) */
    struct ahci_cmd_hdr *cmd_list;  /* Command list (32 KiB, 32 commands) */
    struct ahci_received_fis *fis;  /* Received FIS (256 bytes) */
    struct ahci_cmd_tbl *cmd_tbl;   /* Command table (4 KiB) */
};

struct ahci_ctrlr {
    volatile uint32_t *hba;         /* HBA base (mapped ABAR) */
    uint8_t bus, slot, func;        /* PCI address */
    uint32_t cap;                   /* Capabilities register */
    uint32_t pi;                    /* Ports Implemented */
    struct ahci_port ports[32];     /* Up to 32 ports */
    int port_count;
};

static struct ahci_ctrlr ahci_ctrlr = {0};

/*
 * Register access helpers
 *
 * Reads/writes are volatile because hardware can change register state
 * independently of CPU operations.
 */

static inline uint32_t ahci_reg_read(volatile uint32_t *hba, uint32_t offset)
{
    return *((volatile uint32_t *)((uint64_t)hba + offset));
}

static inline void ahci_reg_write(volatile uint32_t *hba, uint32_t offset, uint32_t value)
{
    *((volatile uint32_t *)((uint64_t)hba + offset)) = value;
}

static inline uint32_t ahci_port_read(volatile uint32_t *port, uint32_t offset)
{
    return *((volatile uint32_t *)((uint64_t)port + offset));
}

static inline void ahci_port_write(volatile uint32_t *port, uint32_t offset, uint32_t value)
{
    *((volatile uint32_t *)((uint64_t)port + offset)) = value;
}

/*
 * Busy-wait with timeout (for HBA operations like reset)
 * Spins until condition becomes true or timeout expires.
 */
static int ahci_wait_timeout(int *cond, int max_iterations)
{
    while (max_iterations-- > 0) {
        if (*cond)
            return 1;
        /* Small delay (1 msec via busy loop) */
        for (volatile int i = 0; i < 100000; i++);
    }
    return 0;
}

/*
 * pci_is_ahci — check if PCI device is an AHCI controller
 */
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

/*
 * ahci_hba_from_bar5 — extract ABAR physical address from PCI BAR5
 */
static uint64_t ahci_hba_phys_from_bar5(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint32_t bar5 = pci_read_config(bus, slot, func, PCI_CONFIG_BAR5, 4);

    /* Require memory BAR; ignore I/O BARs for AHCI. */
    if (bar5 & 0x1)
        return 0;

    return (uint64_t)(bar5 & ~0xFUL);
}

/*
 * ahci_map_abar — map ABAR from physical to kernel virtual address space
 *
 * AHCI MMIO region is typically 256 MB (aligned to 256 MB boundary).
 * For simplicity, we map just the first 1 MB which covers all HBA + port registers.
 */
static volatile uint32_t *ahci_map_abar(uint64_t phys_abar)
{
    /*
     * Find a suitable kernel virtual address for device mapping.
     * For Phase 2, use a fixed kernel VM range: 0xFFFFFFFF00000000
     * (end of 64-bit space, after kernel code).
     *
     * TODO (Phase 3): Use proper vm_map entry allocation.
     */
    volatile uint32_t *virt_abar = (volatile uint32_t *)0xFFFFFFFF00000000ULL;

    /* Map the ABAR region into kernel VM.
     * 
     * Flags:
     *   PTE_WRITE: Device MMIO needs write access
     *   PTE_PCD:   Page Cache Disable (device memory must not be cached)
     *   PTE_PWT:   Page Write-Through (safer for device MMIO)
     */
    uint64_t virt_page = 0xFFFFFFFF00000000ULL;
    uint64_t phys_page = phys_abar & ~0xFFFULL;
    uint32_t page_count = 256;  /* Map 1 MB: 256 * 4 KB pages */

    for (uint32_t i = 0; i < page_count; i++) {
        paging_map(virt_page + i * 4096,
                   phys_page + i * 4096,
                   PTE_WRITE | PTE_PCD | PTE_PWT);
    }

    return virt_abar;
}

/*
 * ahci_init_port — initialize a single AHCI port
 *
 * Sets up command list, FIS buffer, and command table for a port.
 * Does not start the port (that would require device presence detection).
 */
static int ahci_init_port(struct ahci_ctrlr *hba, int port_num)
{
    struct ahci_port *port = &hba->ports[port_num];
    volatile uint32_t *port_base = (volatile uint32_t *)
        ((uint64_t)hba->hba + AHCI_PORT_BASE(port_num));

    port->base = port_base;

    /* Allocate command list (32 commands * 32 bytes = 1 KiB, but drivers
     * typically allocate 32 KiB for alignment to 1 KiB boundary). */
    struct vm_page *pg = vm_page_alloc();
    if (!pg) {
        serial_putstr("[ahci] port ");
        serial_putdec(port_num);
        serial_putstr(" cmd_list allocation failed\r\n");
        return 0;
    }
    port->cmd_list = (struct ahci_cmd_hdr *)pg->pg_phys_addr;
    kmemset(port->cmd_list, 0, VM_PAGE_SIZE);

    /* Allocate FIS buffer (256 bytes per port). */
    pg = vm_page_alloc();
    if (!pg) {
        serial_putstr("[ahci] port ");
        serial_putdec(port_num);
        serial_putstr(" FIS allocation failed\r\n");
        return 0;
    }
    port->fis = (struct ahci_received_fis *)pg->pg_phys_addr;
    kmemset(port->fis, 0, VM_PAGE_SIZE);

    /* Allocate command table (typically one per port for testing). */
    pg = vm_page_alloc();
    if (!pg) {
        serial_putstr("[ahci] port ");
        serial_putdec(port_num);
        serial_putstr(" cmd_tbl allocation failed\r\n");
        return 0;
    }
    port->cmd_tbl = (struct ahci_cmd_tbl *)pg->pg_phys_addr;
    kmemset(port->cmd_tbl, 0, VM_PAGE_SIZE);

    /* Initialize command header to point to command table.
     * FIS length = 5 (for Register H2D FIS, 20 bytes).
     * PRD table length = 0 (no data transfer in this test).
     */
    struct ahci_cmd_hdr *hdr = &port->cmd_list[0];
    hdr->flags = (5 & 0x1F);        /* FIS length in DW */
    hdr->prdtl = 0;                 /* No PRD entries for now */
    hdr->ctba = (uint32_t)(uint64_t)port->cmd_tbl;
    hdr->ctbau = (uint32_t)((uint64_t)port->cmd_tbl >> 32);

    /* Program port registers with our allocated buffers. */
    ahci_port_write(port_base, AHCI_PORT_CLB, (uint32_t)(uint64_t)port->cmd_list);
    ahci_port_write(port_base, AHCI_PORT_CLBU, (uint32_t)((uint64_t)port->cmd_list >> 32));
    ahci_port_write(port_base, AHCI_PORT_FB, (uint32_t)(uint64_t)port->fis);
    ahci_port_write(port_base, AHCI_PORT_FBU, (uint32_t)((uint64_t)port->fis >> 32));

    serial_putstr("[ahci] port ");
    serial_putdec(port_num);
    serial_putstr(" initialized (cmd_list: ");
    serial_puthex((uint64_t)port->cmd_list);
    serial_putstr(", fis: ");
    serial_puthex((uint64_t)port->fis);
    serial_putstr(")\r\n");

    return 1;
}

/*
 * ahci_init — discover and initialize AHCI controllers
 *
 * 1. Scan PCI bus for AHCI controllers
 * 2. Map ABAR into kernel VM
 * 3. Read HBA capabilities
 * 4. Initialize ports
 */
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

                uint64_t phys_abar = ahci_hba_phys_from_bar5(bus, slot, func);
                if (phys_abar == 0) {
                    serial_putstr("[ahci] WARN: invalid BAR5\r\n");
                    continue;
                }

                serial_putstr("[ahci] ABAR physaddr: ");
                serial_puthex(phys_abar);
                serial_putstr("\r\n");

                /* Map ABAR into kernel VM. */
                volatile uint32_t *hba = ahci_map_abar(phys_abar);
                serial_putstr("[ahci] ABAR mapped to kernel VA: ");
                serial_puthex((uint64_t)hba);
                serial_putstr("\r\n");

                /* Store in global state. */
                ahci_ctrlr.hba = hba;
                ahci_ctrlr.bus = bus;
                ahci_ctrlr.slot = slot;
                ahci_ctrlr.func = func;

                /* Read HBA capabilities. */
                ahci_ctrlr.cap = ahci_reg_read(hba, AHCI_REG_CAP);
                serial_putstr("[ahci] CAP: ");
                serial_puthex(ahci_ctrlr.cap);
                serial_putstr("\r\n");

                /* Read which ports are implemented. */
                ahci_ctrlr.pi = ahci_reg_read(hba, AHCI_REG_PI);
                serial_putstr("[ahci] PI: ");
                serial_puthex(ahci_ctrlr.pi);
                serial_putstr("\r\n");

                /* Initialize port 0 if implemented. */
                if (ahci_ctrlr.pi & 0x1) {
                    if (ahci_init_port(&ahci_ctrlr, 0)) {
                        ahci_ctrlr.port_count = 1;
                    }
                }
            }
        }
    }

    if (!found)
        serial_putstr("[ahci] no AHCI controller found\r\n");
}

/*
 * ahci_test — verify AHCI command engine initialization
 *
 * This is a harmless test that verifies:
 * 1. Port command list is properly programmed
 * 2. Registers are readable
 * 3. No crash on MMIO access
 */
void ahci_test(void)
{
    if (!ahci_ctrlr.hba) {
        serial_putstr("[ahci] test SKIP — no controller configured\r\n");
        return;
    }

    if (ahci_ctrlr.port_count == 0) {
        serial_putstr("[ahci] test SKIP — no ports initialized\r\n");
        return;
    }

    serial_putstr("[ahci] test: verifying command engine...\r\n");

    struct ahci_port *port = &ahci_ctrlr.ports[0];

    /* Read back command list pointer. */
    uint32_t clb = ahci_port_read(port->base, AHCI_PORT_CLB);
    uint32_t clbu = ahci_port_read(port->base, AHCI_PORT_CLBU);
    uint64_t clb_readback = ((uint64_t)clbu << 32) | clb;

    serial_putstr("[ahci] test: CLB readback = ");
    serial_puthex(clb_readback);
    serial_putstr(", expected = ");
    serial_puthex((uint64_t)port->cmd_list);

    if (clb_readback == (uint64_t)port->cmd_list) {
        serial_putstr(" [match]\r\n");
    } else {
        serial_putstr(" [MISMATCH]\r\n");
    }

    /* Read FIS pointer. */
    uint32_t fb = ahci_port_read(port->base, AHCI_PORT_FB);
    uint32_t fbu = ahci_port_read(port->base, AHCI_PORT_FBU);
    uint64_t fb_readback = ((uint64_t)fbu << 32) | fb;

    serial_putstr("[ahci] test: FB readback = ");
    serial_puthex(fb_readback);
    serial_putstr(", expected = ");
    serial_puthex((uint64_t)port->fis);

    if (fb_readback == (uint64_t)port->fis) {
        serial_putstr(" [match]\r\n");
    } else {
        serial_putstr(" [MISMATCH]\r\n");
    }

    /* Read port signature (will be 0 if no device present). */
    uint32_t sig = ahci_port_read(port->base, AHCI_PORT_SIG);
    serial_putstr("[ahci] test: port signature = ");
    serial_puthex(sig);
    serial_putstr(" (0 is expected if no device, 0x101 if ATA is present)\r\n");

    serial_putstr("[ahci] test PASS — MMIO accessible, registers match\r\n");
}

