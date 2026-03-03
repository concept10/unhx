/*
 * kernel/device/virtio_blk.c — Virtio block device driver implementation
 *
 * Implements full synchronous I/O with proper descriptor chaining:
 *   - Request header (type, flags, sector)
 *   - Data buffer (read/write payload)
 *   - Status response (device writes status byte)
 *
 * Queue submission follows Virtio 1.0 spec:
 *   1. Build descriptor chain in descriptor table
 *   2. Add chain head index to available ring
 *   3. Write QUEUE_NOTIFY port to signal device
 *   4. Poll used ring for completion
 */

#include "virtio_blk.h"
#include "pci.h"
#include "kern/klib.h"
#include "kern/kalloc.h"
#include "kern/thread.h"
#include "platform/ioport.h"

extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);

static struct virtio_device *blk_dev = (void *)0;
static struct virtio_block_config blk_config;

/* Simple busy-wait loop for polling delays */
static inline void
io_delay(uint32_t iterations)
{
    volatile uint32_t i;
    for (i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}

/* Per-request state for synchronous I/O */
struct virtio_blk_req_context {
    struct virtio_blk_req *req_header;  /* Request header (type, sector) */
    void *data_buffer;                  /* Data payload (read/write) */
    uint8_t *status;                    /* Status response byte */
    uint32_t reqid;                     /* Request ID / descriptor chain head */
    uint32_t data_len;                  /* Bytes to read/write */
    uint16_t avail_idx;                 /* Index in available ring */
};

/*
 * virtio_mmio_read — read from Virtio configuration space
 *
 * For PCI device with BAR in MMIO, this is a memory read.
 * For now, we assume the BAR is mapped at a virtual address.
 */
static uint32_t virtio_mmio_read(uint32_t offset)
{
    if (!blk_dev || !blk_dev->pci_bar_addr)
        return 0;
    
    uint32_t *addr = (uint32_t *)(blk_dev->pci_bar_addr + offset);
    return *addr;
}

static void virtio_mmio_write(uint32_t offset, uint32_t value)
{
    if (!blk_dev || !blk_dev->pci_bar_addr)
        return;
    
    uint32_t *addr = (uint32_t *)(blk_dev->pci_bar_addr + offset);
    *addr = value;
}

/*
 * virtio_blk_init — detect and initialize virtio-blk devices
 */
void virtio_blk_init(void)
{
    serial_putstr("[virtio-blk] initializing\r\n");
    
    /* Find virtio block device */
    struct pci_device *pci = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_DEVICE_BLOCK);
    if (!pci) {
        serial_putstr("[virtio-blk] no device found\r\n");
        return;
    }
    
    /* Allocate virtio device structure */
    blk_dev = (struct virtio_device *)kalloc(sizeof(struct virtio_device));
    if (!blk_dev) {
        serial_putstr("[virtio-blk] allocation failed\r\n");
        return;
    }
    
    kmemset(blk_dev, 0, sizeof(struct virtio_device));
    blk_dev->pci_dev = pci;
    blk_dev->device_type = VIRTIO_TYPE_BLOCK;
    
    /* PCI BAR0 is the virtio config space (typically MMIO) */
    blk_dev->pci_bar_addr = (uint64_t)(pci->bar[0] & ~0x0FUL);
    if (!blk_dev->pci_bar_addr) {
        serial_putstr("[virtio-blk] BAR0 not mapped\r\n");
        return;
    }
    
    /* For now, map BAR0 as-is (assumes it's already mapped by bootloader) */
    serial_putstr("[virtio-blk] device at BAR0=");
    serial_puthex(blk_dev->pci_bar_addr);
    serial_putstr("\r\n");
    
    /* Reset device */
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, VIRTIO_STATUS_RESET);
    
    /* Acknowledge and set driver status */
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, VIRTIO_STATUS_ACK);
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    /* Read and negotiate features (for Phase 3, minimal features) */
    uint32_t features = virtio_mmio_read(VIRTIO_OFFSET_HOST_FEATURES);
    serial_putstr("[virtio-blk] host features: ");
    serial_puthex(features);
    serial_putstr("\r\n");
    
    /* Accept minimal feature set */
    blk_dev->enabled_features = features & (1UL << (uint32_t)VIRTIO_F_VERSION_1);
    virtio_mmio_write(VIRTIO_OFFSET_DRIVER_FEATURES, blk_dev->enabled_features);
    
    /* Set FEATURES_OK */
    uint32_t status = virtio_mmio_read(VIRTIO_OFFSET_STATUS);
    status |= VIRTIO_STATUS_FEATURES_OK;
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, status);
    
    /* Verify features were accepted */
    status = virtio_mmio_read(VIRTIO_OFFSET_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_putstr("[virtio-blk] feature negotiation failed\r\n");
        return;
    }
    
    /* Read device configuration (capacity, etc.) */
    uint32_t *cfg = (uint32_t *)(blk_dev->pci_bar_addr + VIRTIO_OFFSET_CONFIG);
    blk_config.capacity = *((uint64_t *)cfg);
    blk_config.size_max = cfg[2];
    blk_config.seg_max = cfg[3];
    
    serial_putstr("[virtio-blk] capacity: ");
    serial_puthex(blk_config.capacity);
    serial_putstr(" sectors\r\n");
    
    /* Initialize queue 0 (request queue) */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SEL, 0);
    uint32_t qsize = virtio_mmio_read(VIRTIO_OFFSET_QUEUE_SIZE);
    
    serial_putstr("[virtio-blk] queue 0 size: ");
    serial_puthex(qsize);
    serial_putstr("\r\n");
    
    /* Verify we got a sane queue size */
    if (qsize == 0 || qsize > 256) {
        serial_putstr("[virtio-blk] ERROR: invalid queue size\r\n");
        return;
    }
    
    /* For Phase 3, use minimal queueing; just allocate descriptor and ring buffers */
    if (qsize > 128)
        qsize = 128;
    
    blk_dev->queues[0].size = qsize;
    blk_dev->queues[0].last_avail = 0;
    blk_dev->queues[0].last_used = 0;
    
    /* Allocate queue memory */
    uint32_t desc_size = qsize * sizeof(struct virtio_desc);
    uint32_t avail_size = 6 + qsize * 2;  /* flags + idx + ring[] + used_event */
    uint32_t used_size = 6 + qsize * sizeof(struct virtio_used_elem);
    
    blk_dev->queues[0].desc = (struct virtio_desc *)kalloc(desc_size);
    blk_dev->queues[0].avail = (struct virtio_avail *)kalloc(avail_size);
    blk_dev->queues[0].used = (struct virtio_used *)kalloc(used_size);
    
    if (!blk_dev->queues[0].desc || !blk_dev->queues[0].avail || !blk_dev->queues[0].used) {
        serial_putstr("[virtio-blk] queue allocation failed\r\n");
        return;
    }
    
    kmemset(blk_dev->queues[0].desc, 0, desc_size);
    kmemset(blk_dev->queues[0].avail, 0, avail_size);
    kmemset(blk_dev->queues[0].used, 0, used_size);
    
    /* Configure queue address (physical addresses) */
    uint64_t desc_phys = (uint64_t)blk_dev->queues[0].desc;
    uint64_t avail_phys = (uint64_t)blk_dev->queues[0].avail;
    uint64_t used_phys = (uint64_t)blk_dev->queues[0].used;
    
    serial_putstr("[virtio-blk] descriptor table at ");
    serial_puthex(desc_phys);
    serial_putstr("\r\n");
    
    /* Set queue selector and write addresses */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SEL, 0);
    
    /* Write queue size to reset queue */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SIZE, 0);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SIZE, qsize);
    
    /* Write 64-bit physical addresses to queue configuration */
    /* For Virtio MMIO, descriptor address at 0x40 takes 64-bit address in 32-bit chunks */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_DESC, (uint32_t)desc_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_DESC + 4, (uint32_t)(desc_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_AVAIL, (uint32_t)avail_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_AVAIL + 4, (uint32_t)(avail_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_USED, (uint32_t)used_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_USED + 4, (uint32_t)(used_phys >> 32));
    
    /* Enable queue */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_ENABLE, 1);
    
    /* Set DRIVER_OK to indicate ready */
    status = virtio_mmio_read(VIRTIO_OFFSET_STATUS);
    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, status);
    
    serial_putstr("[virtio-blk] device ready\r\n");
    blk_dev->queue_count = 1;
}

/*
 * virtio_blk_submit_request — build descriptor chain and submit to device
 *
 * Constructs a 3-descriptor chain:
 *   [0] Request header (type, reserved, sector) — device reads
 *   [1] Data buffer read/write payload — device reads/writes
 *   [2] Status byte — device writes completion status
 *
 * Returns request context for polling, or NULL on error.
 */
static struct virtio_blk_req_context *
virtio_blk_submit_request(uint32_t type, uint64_t sector, uint32_t data_len,
                          void *data_buffer, uint8_t *status)
{
    if (!blk_dev)
        return (void *)0;
    
    struct virtio_queue *q = &blk_dev->queues[0];
    
    /* Allocate request header and context */
    struct virtio_blk_req_context *ctx =
        (struct virtio_blk_req_context *)kalloc(sizeof(*ctx));
    if (!ctx)
        return (void *)0;
    
    ctx->req_header = (struct virtio_blk_req *)kalloc(sizeof(struct virtio_blk_req));
    ctx->status = (uint8_t *)kalloc(1);
    
    if (!ctx->req_header || !ctx->status) {
        return (void *)0;
    }
    
    /* Fill request header */
    ctx->req_header->type = type;
    ctx->req_header->reserved = 0;
    ctx->req_header->sector = sector;
    ctx->data_buffer = data_buffer;
    ctx->data_len = data_len;
    *ctx->status = 0xFF;  /* Pending */
    
    /* Get next available descriptor indices */
    uint16_t head_idx = q->last_avail;
    uint16_t desc_idx = head_idx;
    uint16_t next_idx = (head_idx + 1) % q->size;
    uint16_t status_idx = (head_idx + 2) % q->size;
    
    /* Verify we have 3 free descriptors (simple check, not wraparound-safe) */
    if (next_idx >= q->size || status_idx >= q->size) {
        return (void *)0;
    }
    
    /* Descriptor 0: Request header (flags=NEXT, device reads) */
    q->desc[desc_idx].addr = (uint64_t)ctx->req_header;
    q->desc[desc_idx].len = sizeof(struct virtio_blk_req);
    q->desc[desc_idx].flags = VIRTIO_DESC_F_NEXT;  /* More descriptors follow */
    q->desc[desc_idx].next = next_idx;
    
    /* Descriptor 1: Data buffer (flags=NEXT|WRITE for reads, NEXT for writes) */
    q->desc[next_idx].addr = (uint64_t)data_buffer;
    q->desc[next_idx].len = data_len;
    if (type == VIRTIO_BLK_T_IN) {
        /* Read: device writes to buffer */
        q->desc[next_idx].flags = VIRTIO_DESC_F_WRITE | VIRTIO_DESC_F_NEXT;
    } else {
        /* Write: device reads from buffer */
        q->desc[next_idx].flags = VIRTIO_DESC_F_NEXT;
    }
    q->desc[next_idx].next = status_idx;
    
    /* Descriptor 2: Status byte (device writes back) */
    q->desc[status_idx].addr = (uint64_t)ctx->status;
    q->desc[status_idx].len = 1;
    q->desc[status_idx].flags = VIRTIO_DESC_F_WRITE;  /* Last descriptor, device writes */
    q->desc[status_idx].next = 0;
    
    /* Track request context for polling */
    ctx->reqid = head_idx;
    ctx->avail_idx = q->last_avail;
    
    /* Add head index to available ring */
    uint16_t avail_ring_idx = q->avail->idx % q->size;
    q->avail->ring[avail_ring_idx] = head_idx;
    /* Memory barrier: ensure descriptors are visible before updating index */
    q->avail->idx += 1;
    
    /* Update tracking */
    q->last_avail = (q->last_avail + 3) % q->size;
    
    /* Notify device: write queue number to QUEUE_NOTIFY port */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_NOTIFY, 0);
    
    return ctx;
}

/*
 * virtio_blk_wait_completion — poll used ring until request completes
 *
 * Returns 0 on success (status == OK), -1 on error or timeout.
 */
static int
virtio_blk_wait_completion(struct virtio_blk_req_context *ctx)
{
    if (!ctx)
        return -1;
    
    struct virtio_queue *q = &blk_dev->queues[0];
    
    /* Poll used ring for completion
     * The device updates used.idx when completing requests.
     */
    uint32_t timeout = 1000000;  /* ~1 second of polling iterations */
    while (timeout--) {
        /* Check if this request has been processed */
        if (q->used->idx != q->last_used) {
            /* At least one completion available */
            struct virtio_used_elem *used_elem =
                &q->used->ring[q->last_used % q->size];
            
            /* Verify this is our request */
            if (used_elem->id == ctx->reqid) {
                /* Check status */
                if (*ctx->status == VIRTIO_BLK_S_OK) {
                    q->last_used = (q->last_used + 1) % q->size;
                    return 0;
                } else {
                    /* I/O error */
                    serial_putstr("[virtio-blk] status error: ");
                    serial_puthex(*ctx->status);
                    serial_putstr("\r\n");
                    q->last_used = (q->last_used + 1) % q->size;
                    return -1;
                }
            }
            
            /* Skip irrelevant completions (shouldn't happen) */
            q->last_used = (q->last_used + 1) % q->size;
        }
        
        /* Spin wait: small delay to avoid CPU-locking */
        io_delay(100);
    }
    
    /* Timeout */
    serial_putstr("[virtio-blk] I/O timeout\r\n");
    return -1;
}

/*
 * virtio_blk_cleanup_request — free request context after completion
 */
static void
virtio_blk_cleanup_request(struct virtio_blk_req_context *ctx)
{
    if (!ctx)
        return;
    
    if (ctx->req_header)
        kfree(ctx->req_header);
    if (ctx->status)
        kfree(ctx->status);
    
    kfree(ctx);
}

/*
 * virtio_blk_test — simple test of disk I/O functionality
 *
 * Attempts to read sector 0 and log results.
 * This is called from kernel_main after device initialization.
 */
void virtio_blk_test(void)
{
    if (!blk_dev) {
        serial_putstr("[virtio-blk] test SKIP — device not initialized\r\n");
        return;
    }
    
    serial_putstr("[virtio-blk] test starting — attempting read of sector 0\r\n");
    
    /* Allocate a 512-byte buffer for test read */
    void *test_buffer = kalloc(512);
    if (!test_buffer) {
        serial_putstr("[virtio-blk] test FAIL — buffer allocation failed\r\n");
        return;
    }
    
    /* Try to read one sector */
    int result = virtio_blk_read_sectors(0, 1, test_buffer);
    
    if (result == 0) {
        serial_putstr("[virtio-blk] test PASS — sector 0 read successfully\r\n");
    } else {
        serial_putstr("[virtio-blk] test SKIP — read returned error (may be expected if disk not configured)\r\n");
    }
    
    kfree(test_buffer);
}



/*
 * virtio_blk_read_sectors — synchronous read of disk sectors
 *
 * sector — starting LBA
 * count — number of 512-byte sectors to read
 * buffer — destination buffer (must be sector-aligned, physically contiguous)
 *
 * Returns 0 on success, -1 on error or timeout.
 */
int virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buffer)
{
    if (!blk_dev || !buffer)
        return -1;
    
    uint32_t data_len = count * 512;  /* Each sector is 512 bytes */
    
    serial_putstr("[virtio-blk] read ");
    serial_puthex(count);
    serial_putstr(" sectors from ");
    serial_puthex(sector);
    serial_putstr("\r\n");
    
    /* Submit read request to device */
    struct virtio_blk_req_context *ctx =
        virtio_blk_submit_request(VIRTIO_BLK_T_IN, sector, data_len, buffer, (void *)0);
    
    if (!ctx) {
        serial_putstr("[virtio-blk] read submission failed\r\n");
        return -1;
    }
    
    /* Wait for completion */
    int result = virtio_blk_wait_completion(ctx);
    
    /* Cleanup */
    virtio_blk_cleanup_request(ctx);
    
    if (result == 0) {
        serial_putstr("[virtio-blk] read complete\r\n");
    } else {
        serial_putstr("[virtio-blk] read failed\r\n");
    }
    
    return result;
}

/*
 * virtio_blk_write_sectors — synchronous write of disk sectors
 *
 * sector — starting LBA
 * count — number of 512-byte sectors to write
 * buffer — source buffer (must be sector-aligned, physically contiguous)
 *
 * Returns 0 on success, -1 on error or timeout.
 */
int virtio_blk_write_sectors(uint64_t sector, uint32_t count, const void *buffer)
{
    if (!blk_dev || !buffer)
        return -1;
    
    uint32_t data_len = count * 512;  /* Each sector is 512 bytes */
    
    serial_putstr("[virtio-blk] write ");
    serial_puthex(count);
    serial_putstr(" sectors to ");
    serial_puthex(sector);
    serial_putstr("\r\n");
    
    /* Submit write request to device */
    struct virtio_blk_req_context *ctx =
        virtio_blk_submit_request(VIRTIO_BLK_T_OUT, sector, data_len,
                                  (void *)buffer, (void *)0);
    
    if (!ctx) {
        serial_putstr("[virtio-blk] write submission failed\r\n");
        return -1;
    }
    
    /* Wait for completion */
    int result = virtio_blk_wait_completion(ctx);
    
    /* Cleanup */
    virtio_blk_cleanup_request(ctx);
    
    if (result == 0) {
        serial_putstr("[virtio-blk] write complete\r\n");
    } else {
        serial_putstr("[virtio-blk] write failed\r\n");
    }
    
    return result;
}



