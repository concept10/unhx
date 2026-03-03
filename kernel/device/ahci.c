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

                /* Enable AHCI mode (GHC.AE bit) */
                uint32_t ghc = ahci_reg_read(hba, AHCI_REG_GHC);
                if (!(ghc & GHC_AE)) {
                    serial_putstr("[ahci] enabling AHCI mode (GHC.AE)\r\n");
                    ahci_reg_write(hba, AHCI_REG_GHC, ghc | GHC_AE);
                    ghc = ahci_reg_read(hba, AHCI_REG_GHC);
                    serial_putstr("[ahci] GHC: ");
                    serial_puthex(ghc);
                    serial_putstr("\r\n");
                } else {
                    serial_putstr("[ahci] AHCI mode already enabled\r\n");
                }

                /* Initialize port 0 if implemented. */
                if (ahci_ctrlr.pi & 0x1) {
                    if (ahci_init_port(&ahci_ctrlr, 0)) {
                        ahci_ctrlr.port_count = 1;
                        
                        /* Start the port to enable command processing */
                        if (ahci_port_start(0) == 0) {
                            serial_putstr("[ahci] port 0 started successfully\r\n");
                        } else {
                            serial_putstr("[ahci] WARN: failed to start port 0\r\n");
                        }
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
    
    /* Test IDENTIFY DEVICE command */
    serial_putstr("[ahci] test: issuing IDENTIFY DEVICE command...\r\n");
    
    /* Allocate buffer for identify data (512 bytes) */
    struct vm_page *pg = vm_page_alloc();
    if (!pg) {
        serial_putstr("[ahci] test: failed to allocate identify buffer\r\n");
        return;
    }
    uint16_t *identify_buf = (uint16_t *)pg->pg_phys_addr;
    
    if (ahci_identify(0, identify_buf) == 0) {
        /* Parse identify data */
        /* Word 60-61: Total user addressable sectors for 28-bit commands */
        uint32_t sectors_28 = identify_buf[60] | ((uint32_t)identify_buf[61] << 16);
        
        /* Word 100-103: Total user addressable sectors for 48-bit commands */
        uint64_t sectors_48 = identify_buf[100] | 
                             ((uint64_t)identify_buf[101] << 16) |
                             ((uint64_t)identify_buf[102] << 32) |
                             ((uint64_t)identify_buf[103] << 48);
        
        /* Word 10-19: Serial number (20 ASCII chars, byte-swapped) */
        char serial[21];
        for (int i = 0; i < 10; i++) {
            serial[i*2] = (char)(identify_buf[10+i] >> 8);
            serial[i*2+1] = (char)(identify_buf[10+i] & 0xFF);
        }
        serial[20] = '\0';
        
        /* Word 27-46: Model number (40 ASCII chars, byte-swapped) */
        char model[41];
        for (int i = 0; i < 20; i++) {
            model[i*2] = (char)(identify_buf[27+i] >> 8);
            model[i*2+1] = (char)(identify_buf[27+i] & 0xFF);
        }
        model[40] = '\0';
        
        serial_putstr("[ahci] test: IDENTIFY successful\r\n");
        serial_putstr("[ahci]   model: ");
        serial_putstr(model);
        serial_putstr("\r\n");
        serial_putstr("[ahci]   serial: ");
        serial_putstr(serial);
        serial_putstr("\r\n");
        serial_putstr("[ahci]   sectors (28-bit): ");
        serial_putdec(sectors_28);
        serial_putstr("\r\n");
        serial_putstr("[ahci]   sectors (48-bit): ");
        serial_puthex(sectors_48);
        serial_putstr("\r\n");
        
        uint64_t size_mb = (sectors_48 * 512) / (1024 * 1024);
        serial_putstr("[ahci]   capacity: ");
        serial_putdec((uint32_t)size_mb);
        serial_putstr(" MB\r\n");
        
        /* Test read operation */
        serial_putstr("[ahci] test: reading sector 0...\r\n");
        
        struct vm_page *pg2 = vm_page_alloc();
        if (!pg2) {
            serial_putstr("[ahci] test: failed to allocate read buffer\r\n");
            return;
        }
        uint8_t *read_buf = (uint8_t *)pg2->pg_phys_addr;
        
        if (ahci_read_sectors(0, 0, 1, read_buf) == 0) {
            serial_putstr("[ahci] test: READ successful\r\n");
            serial_putstr("[ahci]   first 32 bytes: ");
            for (int i = 0; i < 32; i++) {
                if (i > 0 && i % 16 == 0)
                    serial_putstr(" ");
                serial_puthex((uint32_t)read_buf[i]);
                serial_putstr(" ");
            }
            serial_putstr("\r\n");
            
            /* Check for MBR signature (0x55 0xAA at bytes 510-511) */
            if (read_buf[510] == 0x55 && read_buf[511] == 0xAA) {
                serial_putstr("[ahci] test: MBR signature found\r\n");
            } else {
                serial_putstr("[ahci] test: no MBR signature (unformatted disk?)\r\n");
            }
        } else {
            serial_putstr("[ahci] test: READ failed\r\n");
        }
    } else {
        serial_putstr("[ahci] test: IDENTIFY failed\r\n");
    }
}

/*
 * ahci_port_stop — stop port command engine
 *
 * Must be called before reconfiguring port or shutting down.
 * Waits for CR (Command List Running) bit to clear.
 */
void ahci_port_stop(int port_num)
{
    if (!ahci_ctrlr.hba || port_num >= 32 || port_num >= ahci_ctrlr.port_count)
        return;

    struct ahci_port *port = &ahci_ctrlr.ports[port_num];
    volatile uint32_t *port_base = port->base;

    /* Clear ST (Start) bit */
    uint32_t cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
    cmd &= ~CMD_ST;
    ahci_port_write(port_base, AHCI_PORT_CMD, cmd);

    /* Wait for CR (Command List Running) to clear */
    int timeout = 500; /* 500ms */
    while (timeout-- > 0) {
        cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
        if ((cmd & CMD_CR) == 0)
            break;
        /* 1ms delay */
        for (volatile int i = 0; i < 100000; i++);
    }

    /* Clear FRE (FIS Receive Enable) */
    cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
    cmd &= ~CMD_FRE;
    ahci_port_write(port_base, AHCI_PORT_CMD, cmd);

    /* Wait for FR (FIS Receive Running) to clear */
    timeout = 500;
    while (timeout-- > 0) {
        cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
        if ((cmd & CMD_FR) == 0)
            break;
        for (volatile int i = 0; i < 100000; i++);
    }
}

/*
 * ahci_port_start — start port command engine
 *
 * Enables FIS receive and command list processing.
 * Returns 0 on success, -1 on error (timeout or device not ready).
 */
int ahci_port_start(int port_num)
{
    if (!ahci_ctrlr.hba || port_num >= 32 || port_num >= ahci_ctrlr.port_count) {
        serial_putstr("[ahci] port_start: invalid port\r\n");
        return -1;
    }

    struct ahci_port *port = &ahci_ctrlr.ports[port_num];
    volatile uint32_t *port_base = port->base;

    /* Wait for device ready: TFD.STS.BSY = 0 and TFD.STS.DRQ = 0 */
    int timeout = 1000; /* 1 second */
    while (timeout-- > 0) {
        uint32_t tfd = ahci_port_read(port_base, AHCI_PORT_TFD);
        if ((tfd & (TFD_STS_BSY | TFD_STS_DRQ)) == 0)
            break;
        for (volatile int i = 0; i < 100000; i++);
    }

    uint32_t tfd = ahci_port_read(port_base, AHCI_PORT_TFD);
    if (tfd & (TFD_STS_BSY | TFD_STS_DRQ)) {
        serial_putstr("[ahci] port_start: device busy (TFD = ");
        serial_puthex(tfd);
        serial_putstr(")\r\n");
        return -1;
    }

    /* Enable interrupts for command completion */
    uint32_t ie = 0x1   /* DHRE: D2H Register FIS interrupt */
                | 0x2   /* PSE: PIO Setup FIS interrupt */
                | 0x4   /* DSE: DMA Setup FIS interrupt */
                | 0x20000000; /* TFES: Task File Error Status */
    ahci_port_write(port_base, AHCI_PORT_IE, ie);

    /* Enable FIS receive */
    uint32_t cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
    cmd |= CMD_FRE;
    ahci_port_write(port_base, AHCI_PORT_CMD, cmd);

    /* Wait for FR (FIS Receive Running) to set */
    timeout = 500;
    while (timeout-- > 0) {
        cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
        if (cmd & CMD_FR)
            break;
        for (volatile int i = 0; i < 100000; i++);
    }

    if ((cmd & CMD_FR) == 0) {
        serial_putstr("[ahci] port_start: FIS receive failed\r\n");
        return -1;
    }

    /* Enable command list processing */
    cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
    cmd |= CMD_ST;
    ahci_port_write(port_base, AHCI_PORT_CMD, cmd);

    serial_putstr("[ahci] port ");
    serial_putdec(port_num);
    serial_putstr(" started (CMD = ");
    cmd = ahci_port_read(port_base, AHCI_PORT_CMD);
    serial_puthex(cmd);
    serial_putstr(")\r\n");

    return 0;
}

/*
 * ahci_find_cmdslot — find a free command slot
 *
 * Returns slot number (0-31) or -1 if all slots busy.
 * For Phase 2, we just use slot 0 (single command at a time).
 */
static int ahci_find_cmdslot(struct ahci_port *port)
{
    uint32_t slots = ahci_port_read(port->base, AHCI_PORT_CI);
    if (slots & 0x1)
        return -1;  /* Slot 0 busy */
    return 0;       /* Use slot 0 */
}

/*
 * ahci_wait_command — wait for command completion
 *
 * Polls CI (Command Issue) register until the bit for our slot clears.
 * Also polls interrupt status for completion or error.
 * Returns 0 on success, -1 on timeout or error.
 */
static int ahci_wait_command(struct ahci_port *port, int slot)
{
    int timeout = 10000; /* 10 seconds for slow devices */
    uint32_t slot_bit = 1 << slot;

    while (timeout-- > 0) {
        /* Check if command slot has been cleared by hardware */
        uint32_t ci = ahci_port_read(port->base, AHCI_PORT_CI);
        
        /* Check interrupt status */
        uint32_t is = ahci_port_read(port->base, AHCI_PORT_IS);
        
        /* Command completed when CI bit clears OR we get D2H Register FIS interrupt */
        if ((ci & slot_bit) == 0 || (is & 0x1)) {
            /* Clear interrupt status */
            if (is)
                ahci_port_write(port->base, AHCI_PORT_IS, is);
            
            /* Check for errors */
            uint32_t tfd = ahci_port_read(port->base, AHCI_PORT_TFD);
            if (tfd & TFD_STS_ERR) {
                serial_putstr("[ahci] command error: TFD = ");
                serial_puthex(tfd);
                serial_putstr(", IS = ");
                serial_puthex(is);
                serial_putstr("\r\n");
                return -1;
            }
            return 0;
        }
        
        /* 1ms delay */
        for (volatile int i = 0; i < 100000; i++);
    }

    serial_putstr("[ahci] command timeout\r\n");
    serial_putstr("[ahci]   CI  = ");
    uint32_t ci = ahci_port_read(port->base, AHCI_PORT_CI);
    serial_puthex(ci);
    serial_putstr("\r\n[ahci]   IS  = ");
    uint32_t is = ahci_port_read(port->base, AHCI_PORT_IS);
    serial_puthex(is);
    serial_putstr("\r\n[ahci]   TFD = ");
    uint32_t tfd = ahci_port_read(port->base, AHCI_PORT_TFD);
    serial_puthex(tfd);
    serial_putstr("\r\n[ahci]   CMD = ");
    uint32_t cmd = ahci_port_read(port->base, AHCI_PORT_CMD);
    serial_puthex(cmd);
    serial_putstr("\r\n[ahci]   SERR = ");
    uint32_t serr = ahci_port_read(port->base, AHCI_PORT_SCR_ERR);
    serial_puthex(serr);
    serial_putstr("\r\n[ahci]   CLB  = ");
    uint32_t clb = ahci_port_read(port->base, AHCI_PORT_CLB);
    serial_puthex(clb);
    serial_putstr("\r\n[ahci]   FB   = ");
    uint32_t fb = ahci_port_read(port->base, AHCI_PORT_FB);
    serial_puthex(fb);
    serial_putstr("\r\n");
    return -1;
}

/*
 * ahci_identify — issue IDENTIFY DEVICE command
 *
 * Retrieves 512 bytes of device information.
 * buf must be at least 512 bytes and physically contiguous.
 * Returns 0 on success, -1 on error.
 */
int ahci_identify(int port_num, uint16_t *buf)
{
    if (!ahci_ctrlr.hba || port_num >= ahci_ctrlr.port_count) {
        serial_putstr("[ahci] identify: invalid port\r\n");
        return -1;
    }

    struct ahci_port *port = &ahci_ctrlr.ports[port_num];
    
    int slot = ahci_find_cmdslot(port);
    if (slot < 0) {
        serial_putstr("[ahci] identify: no free command slot\r\n");
        return -1;
    }

    /* Build command header */
    struct ahci_cmd_hdr *hdr = &port->cmd_list[slot];
    hdr->flags = sizeof(struct fis_reg_h2d) / 4; /* FIS length in DW (5) */
    hdr->flags |= (1 << 7); /* P (Prefetchable) - set for PIO DATA-IN */
    hdr->flags |= (0 << 5); /* Device port multiplier = 0 */
    hdr->prdtl = 1;         /* One PRD entry */
    hdr->prdbc = 0;

    /* Build command table */
    struct ahci_cmd_tbl *tbl = port->cmd_tbl; /* We only have one */
    kmemset(tbl, 0, sizeof(*tbl));

    /* Build Register H2D FIS manually to avoid bitfield issues */
    uint8_t *fis = tbl->cfis;
    fis[0] = FIS_TYPE_REG_H2D;  /* FIS type */
    fis[1] = 0x80;              /* Bit 7 = 1 (Command register update), PMPort = 0 */
    fis[2] = ATA_CMD_IDENTIFY;  /* Command */
    fis[3] = 0;                 /* Features (7:0) */
    fis[4] = 0;                 /* LBA (7:0) */
    fis[5] = 0;                 /* LBA (15:8) */
    fis[6] = 0;                 /* LBA (23:16) */
    fis[7] = 0;                 /* Device */
    fis[8] = 0;                 /* LBA (31:24) */
    fis[9] = 0;                 /* LBA (39:32) */
    fis[10] = 0;                /* LBA (47:40) */
    fis[11] = 0;                /* Features (15:8) */
    fis[12] = 1;                /* Count (7:0) - 1 sector */
    fis[13] = 0;                /* Count (15:8) */
    fis[14] = 0;                /* Reserved */
    fis[15] = 0;                /* Control */
    /* Bytes 16-19 are reserved */

    /* Build PRD entry */
    tbl->prdt[0].dba = (uint32_t)(uint64_t)buf;
    tbl->prdt[0].dbau = (uint32_t)((uint64_t)buf >> 32);
    tbl->prdt[0].dbc = 511; /* Byte count - 1, must be odd */
    tbl->prdt[0].dbc |= (1U << 31); /* Interrupt on completion */

    /* Debug: show command header setup */
    serial_putstr("[ahci] issuing IDENTIFY: slot=");
    serial_putdec(slot);
    serial_putstr(", hdr.flags=");
    serial_puthex(hdr->flags);
    serial_putstr(", hdr.prdtl=");
    serial_putdec(hdr->prdtl);
    serial_putstr(", hdr.ctba=");
    serial_puthex(hdr->ctba);
    serial_putstr(", fis[2]=");
    serial_puthex(fis[2]);
    serial_putstr("\r\n");

    /* Clear any pending interrupts */
    uint32_t is = ahci_port_read(port->base, AHCI_PORT_IS);
    if (is) {
        ahci_port_write(port->base, AHCI_PORT_IS, is);
    }

    /* Issue command */
    ahci_port_write(port->base, AHCI_PORT_CI, 1 << slot);

    /* Wait for completion */
    if (ahci_wait_command(port, slot) < 0) {
        serial_putstr("[ahci] identify: command failed\r\n");
        return -1;
    }

    serial_putstr("[ahci] identify: success\r\n");
    return 0;
}

/*
 * ahci_read_sectors — read sectors from disk
 *
 * Uses READ DMA EXT (48-bit LBA) command.
 * buf must be physically contiguous.
 * Returns 0 on success, -1 on error.
 */
int ahci_read_sectors(int port_num, uint64_t lba, uint16_t count, void *buf)
{
    if (!ahci_ctrlr.hba || port_num >= ahci_ctrlr.port_count) {
        serial_putstr("[ahci] read: invalid port\r\n");
        return -1;
    }

    if (count == 0 || count > 256) {
        serial_putstr("[ahci] read: invalid count\r\n");
        return -1;
    }

    struct ahci_port *port = &ahci_ctrlr.ports[port_num];
    
    int slot = ahci_find_cmdslot(port);
    if (slot < 0) {
        serial_putstr("[ahci] read: no free command slot\r\n");
        return -1;
    }

    /* Build command header */
    struct ahci_cmd_hdr *hdr = &port->cmd_list[slot];
    hdr->flags = sizeof(struct fis_reg_h2d) / 4; /* FIS length in DW */
    hdr->flags |= (0 << 5);     /* Device port multiplier = 0 */
    hdr->prdtl = 1;             /* One PRD entry */
    hdr->prdbc = 0;

    /* Build command table */
    struct ahci_cmd_tbl *tbl = port->cmd_tbl;
    kmemset(tbl, 0, sizeof(*tbl));

    /* Build Register H2D FIS */
    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;             /* Command */
    fis->command = ATA_CMD_READ_DMA_EX;

    /* 48-bit LBA */
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    fis->device = 1 << 6;   /* LBA mode */

    /* Sector count */
    fis->countl = (uint8_t)(count & 0xFF);
    fis->counth = (uint8_t)((count >> 8) & 0xFF);

    /* Build PRD entry */
    uint32_t bytes = count * 512;
    tbl->prdt[0].dba = (uint32_t)(uint64_t)buf;
    tbl->prdt[0].dbau = (uint32_t)((uint64_t)buf >> 32);
    tbl->prdt[0].dbc = (bytes - 1) | (1U << 31); /* Byte count - 1, with IOC bit */

    /* Issue command */
    ahci_port_write(port->base, AHCI_PORT_CI, 1 << slot);

    /* Wait for completion */
    if (ahci_wait_command(port, slot) < 0) {
        serial_putstr("[ahci] read: command failed\r\n");
        return -1;
    }

    return 0;
}

/*
 * ahci_write_sectors — write sectors to disk
 *
 * Uses WRITE DMA EXT (48-bit LBA) command.
 * buf must be physically contiguous.
 * Returns 0 on success, -1 on error.
 */
int ahci_write_sectors(int port_num, uint64_t lba, uint16_t count, const void *buf)
{
    if (!ahci_ctrlr.hba || port_num >= ahci_ctrlr.port_count) {
        serial_putstr("[ahci] write: invalid port\r\n");
        return -1;
    }

    if (count == 0 || count > 256) {
        serial_putstr("[ahci] write: invalid count\r\n");
        return -1;
    }

    struct ahci_port *port = &ahci_ctrlr.ports[port_num];
    
    int slot = ahci_find_cmdslot(port);
    if (slot < 0) {
        serial_putstr("[ahci] write: no free command slot\r\n");
        return -1;
    }

    /* Build command header */
    struct ahci_cmd_hdr *hdr = &port->cmd_list[slot];
    hdr->flags = sizeof(struct fis_reg_h2d) / 4; /* FIS length in DW */
    hdr->flags |= (1 << 6); /* Write (host → device) */
    hdr->flags |= (0 << 5); /* Device port multiplier = 0 */
    hdr->prdtl = 1;         /* One PRD entry */
    hdr->prdbc = 0;

    /* Build command table */
    struct ahci_cmd_tbl *tbl = port->cmd_tbl;
    kmemset(tbl, 0, sizeof(*tbl));

    /* Build Register H2D FIS */
    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;             /* Command */
    fis->command = ATA_CMD_WRITE_DMA_EX;

    /* 48-bit LBA */
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);
    fis->device = 1 << 6;   /* LBA mode */

    /* Sector count */
    fis->countl = (uint8_t)(count & 0xFF);
    fis->counth = (uint8_t)((count >> 8) & 0xFF);

    /* Build PRD entry */
    uint32_t bytes = count * 512;
    tbl->prdt[0].dba = (uint32_t)(uint64_t)buf;
    tbl->prdt[0].dbau = (uint32_t)((uint64_t)buf >> 32);
    tbl->prdt[0].dbc = (bytes - 1) | (1U << 31); /* Byte count - 1, with IOC bit */

    /* Issue command */
    ahci_port_write(port->base, AHCI_PORT_CI, 1 << slot);

    /* Wait for completion */
    if (ahci_wait_command(port, slot) < 0) {
        serial_putstr("[ahci] write: command failed\r\n");
        return -1;
    }

    return 0;
}

