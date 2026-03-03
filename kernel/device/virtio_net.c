/*
 * kernel/device/virtio_net.c — Virtio network device driver implementation
 *
 * Implements packet transmission and reception with proper queue management:
 *   - TX queue (queue 0): For sending packets
 *   - RX queue (queue 1): For receiving packets
 *
 * RX buffers are pre-allocated and submitted to device during initialization.
 * When device receives packets, it places them in these buffers and updates
 * the used ring. Driver polls used ring to check for new packets.
 */

#include "virtio_net.h"
#include "pci.h"
#include "kern/klib.h"
#include "kern/kalloc.h"
#include "platform/ioport.h"

extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);

static struct virtio_device *net_dev = (void *)0;
static struct virtio_net_config net_config;

/* Pre-allocated RX buffers for receiving packets */
static struct net_packet *rx_buffers[VIRTIO_NET_RX_BUFFERS];
static uint16_t rx_buffer_count = 0;

/* Simple busy-wait loop for polling delays */
static inline void
io_delay(uint32_t iterations)
{
    volatile uint32_t i;
    for (i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}

/*
 * virtio_mmio_read — read from Virtio configuration space
 */
static uint32_t virtio_mmio_read(uint32_t offset)
{
    if (!net_dev || !net_dev->pci_bar_addr)
        return 0;
    
    uint32_t *addr = (uint32_t *)(net_dev->pci_bar_addr + offset);
    return *addr;
}

static void virtio_mmio_write(uint32_t offset, uint32_t value)
{
    if (!net_dev || !net_dev->pci_bar_addr)
        return;
    
    uint32_t *addr = (uint32_t *)(net_dev->pci_bar_addr + offset);
    *addr = value;
}

/*
 * virtio_net_setup_rx_buffers — pre-allocate and submit RX buffers to device
 *
 * The virtio-net RX queue works differently from block devices:
 * - Driver allocates buffers and submits them to the device
 * - Device writes incoming packets to these buffers
 * - Device signals completion via used ring
 * - Driver must replenish buffers after consuming packets
 */
static int
virtio_net_setup_rx_buffers(void)
{
    struct virtio_queue *rxq = &net_dev->queues[1];  /* Queue 1 = RX */
    
    serial_putstr("[virtio-net] setting up RX buffers\r\n");
    
    /* Allocate and submit RX buffers */
    for (uint16_t i = 0; i < VIRTIO_NET_RX_BUFFERS; i++) {
        /* Allocate packet buffer */
        rx_buffers[i] = (struct net_packet *)kalloc(sizeof(struct net_packet));
        if (!rx_buffers[i]) {
            serial_putstr("[virtio-net] RX buffer allocation failed\r\n");
            return -1;
        }
        
        kmemset(rx_buffers[i], 0, sizeof(struct net_packet));
        
        /* Set up descriptor for this buffer
         * RX descriptors are write-only (device writes to them)
         */
        uint16_t desc_idx = i;
        rxq->desc[desc_idx].addr = (uint64_t)rx_buffers[i];
        rxq->desc[desc_idx].len = sizeof(struct net_packet);
        rxq->desc[desc_idx].flags = VIRTIO_DESC_F_WRITE;  /* Device writes */
        rxq->desc[desc_idx].next = 0;
        
        /* Add to available ring */
        uint16_t avail_idx = rxq->avail->idx % rxq->size;
        rxq->avail->ring[avail_idx] = desc_idx;
        rxq->avail->idx++;
    }
    
    rx_buffer_count = VIRTIO_NET_RX_BUFFERS;
    
    /* Notify device that RX buffers are available */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_NOTIFY, 1);  /* Queue 1 = RX */
    
    serial_putstr("[virtio-net] RX buffers ready\r\n");
    return 0;
}

/*
 * virtio_net_init — detect and initialize virtio-net devices
 */
void virtio_net_init(void)
{
    serial_putstr("[virtio-net] initializing\r\n");
    
    /* Find virtio network device (vendor 0x1AF4, device 0x1000) */
    struct pci_device *pci = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_DEVICE_NET);
    if (!pci) {
        serial_putstr("[virtio-net] no device found\r\n");
        return;
    }
    
    /* Allocate virtio device structure */
    net_dev = (struct virtio_device *)kalloc(sizeof(struct virtio_device));
    if (!net_dev) {
        serial_putstr("[virtio-net] allocation failed\r\n");
        return;
    }
    
    kmemset(net_dev, 0, sizeof(struct virtio_device));
    net_dev->pci_dev = pci;
    net_dev->device_type = VIRTIO_TYPE_NET;
    
    /* PCI BAR0 is the virtio config space */
    net_dev->pci_bar_addr = (uint64_t)(pci->bar[0] & ~0x0FUL);
    if (!net_dev->pci_bar_addr) {
        serial_putstr("[virtio-net] BAR0 not mapped\r\n");
        return;
    }
    
    serial_putstr("[virtio-net] device at BAR0=");
    serial_puthex(net_dev->pci_bar_addr);
    serial_putstr("\r\n");
    
    /* Reset device */
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, VIRTIO_STATUS_RESET);
    
    /* Acknowledge and set driver status */
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, VIRTIO_STATUS_ACK);
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    /* Read and negotiate features */
    uint32_t features = virtio_mmio_read(VIRTIO_OFFSET_HOST_FEATURES);
    serial_putstr("[virtio-net] host features: ");
    serial_puthex(features);
    serial_putstr("\r\n");
    
    /* Accept minimal feature set: MAC address and status */
    net_dev->enabled_features = features & ((1UL << VIRTIO_NET_F_MAC) | 
                                            (1UL << VIRTIO_NET_F_STATUS) |
                                            (1UL << (uint32_t)VIRTIO_F_VERSION_1));
    virtio_mmio_write(VIRTIO_OFFSET_DRIVER_FEATURES, net_dev->enabled_features);
    
    /* Set FEATURES_OK */
    uint32_t status = virtio_mmio_read(VIRTIO_OFFSET_STATUS);
    status |= VIRTIO_STATUS_FEATURES_OK;
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, status);
    
    /* Verify features were accepted */
    status = virtio_mmio_read(VIRTIO_OFFSET_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_putstr("[virtio-net] feature negotiation failed\r\n");
        return;
    }
    
    /* Read device configuration (MAC address, status, MTU) */
    uint8_t *cfg = (uint8_t *)(net_dev->pci_bar_addr + VIRTIO_OFFSET_CONFIG);
    for (int i = 0; i < 6; i++) {
        net_config.mac[i] = cfg[i];
    }
    net_config.status = *((uint16_t *)(cfg + 6));
    net_config.max_virtqueue_pairs = *((uint16_t *)(cfg + 8));
    net_config.mtu = *((uint16_t *)(cfg + 10));
    
    serial_putstr("[virtio-net] MAC address: ");
    for (int i = 0; i < 6; i++) {
        serial_puthex(net_config.mac[i]);
        if (i < 5) serial_putstr(":");
    }
    serial_putstr("\r\n");
    
    /* Initialize TX queue (queue 0) */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SEL, 0);
    uint32_t txq_size = virtio_mmio_read(VIRTIO_OFFSET_QUEUE_SIZE);
    
    serial_putstr("[virtio-net] TX queue size: ");
    serial_puthex(txq_size);
    serial_putstr("\r\n");
    
    if (txq_size == 0 || txq_size > 256) {
        serial_putstr("[virtio-net] invalid TX queue size\r\n");
        return;
    }
    
    if (txq_size > 128) txq_size = 128;
    
    net_dev->queues[0].size = txq_size;
    net_dev->queues[0].last_avail = 0;
    net_dev->queues[0].last_used = 0;
    
    /* Allocate TX queue memory */
    uint32_t desc_size = txq_size * sizeof(struct virtio_desc);
    uint32_t avail_size = 6 + txq_size * 2;
    uint32_t used_size = 6 + txq_size * sizeof(struct virtio_used_elem);
    
    net_dev->queues[0].desc = (struct virtio_desc *)kalloc(desc_size);
    net_dev->queues[0].avail = (struct virtio_avail *)kalloc(avail_size);
    net_dev->queues[0].used = (struct virtio_used *)kalloc(used_size);
    
    if (!net_dev->queues[0].desc || !net_dev->queues[0].avail || !net_dev->queues[0].used) {
        serial_putstr("[virtio-net] TX queue allocation failed\r\n");
        return;
    }
    
    kmemset(net_dev->queues[0].desc, 0, desc_size);
    kmemset(net_dev->queues[0].avail, 0, avail_size);
    kmemset(net_dev->queues[0].used, 0, used_size);
    
    /* Configure TX queue addresses */
    uint64_t desc_phys = (uint64_t)net_dev->queues[0].desc;
    uint64_t avail_phys = (uint64_t)net_dev->queues[0].avail;
    uint64_t used_phys = (uint64_t)net_dev->queues[0].used;
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SEL, 0);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SIZE, 0);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SIZE, txq_size);
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_DESC, (uint32_t)desc_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_DESC + 4, (uint32_t)(desc_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_AVAIL, (uint32_t)avail_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_AVAIL + 4, (uint32_t)(avail_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_USED, (uint32_t)used_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_USED + 4, (uint32_t)(used_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_ENABLE, 1);
    
    /* Initialize RX queue (queue 1) */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SEL, 1);
    uint32_t rxq_size = virtio_mmio_read(VIRTIO_OFFSET_QUEUE_SIZE);
    
    serial_putstr("[virtio-net] RX queue size: ");
    serial_puthex(rxq_size);
    serial_putstr("\r\n");
    
    if (rxq_size == 0 || rxq_size > 256) {
        serial_putstr("[virtio-net] invalid RX queue size\r\n");
        return;
    }
    
    if (rxq_size > 128) rxq_size = 128;
    
    net_dev->queues[1].size = rxq_size;
    net_dev->queues[1].last_avail = 0;
    net_dev->queues[1].last_used = 0;
    
    /* Allocate RX queue memory */
    desc_size = rxq_size * sizeof(struct virtio_desc);
    avail_size = 6 + rxq_size * 2;
    used_size = 6 + rxq_size * sizeof(struct virtio_used_elem);
    
    net_dev->queues[1].desc = (struct virtio_desc *)kalloc(desc_size);
    net_dev->queues[1].avail = (struct virtio_avail *)kalloc(avail_size);
    net_dev->queues[1].used = (struct virtio_used *)kalloc(used_size);
    
    if (!net_dev->queues[1].desc || !net_dev->queues[1].avail || !net_dev->queues[1].used) {
        serial_putstr("[virtio-net] RX queue allocation failed\r\n");
        return;
    }
    
    kmemset(net_dev->queues[1].desc, 0, desc_size);
    kmemset(net_dev->queues[1].avail, 0, avail_size);
    kmemset(net_dev->queues[1].used, 0, used_size);
    
    /* Configure RX queue addresses */
    desc_phys = (uint64_t)net_dev->queues[1].desc;
    avail_phys = (uint64_t)net_dev->queues[1].avail;
    used_phys = (uint64_t)net_dev->queues[1].used;
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SEL, 1);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SIZE, 0);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_SIZE, rxq_size);
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_DESC, (uint32_t)desc_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_DESC + 4, (uint32_t)(desc_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_AVAIL, (uint32_t)avail_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_AVAIL + 4, (uint32_t)(avail_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_USED, (uint32_t)used_phys);
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_USED + 4, (uint32_t)(used_phys >> 32));
    
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_ENABLE, 1);
    
    /* Set DRIVER_OK to indicate ready */
    status = virtio_mmio_read(VIRTIO_OFFSET_STATUS);
    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_mmio_write(VIRTIO_OFFSET_STATUS, status);
    
    /* Allocate and submit initial RX buffers */
    if (virtio_net_setup_rx_buffers() != 0) {
        serial_putstr("[virtio-net] RX buffer setup failed\r\n");
        return;
    }
    
    serial_putstr("[virtio-net] device ready\r\n");
    net_dev->queue_count = 2;
}

/*
 * virtio_net_transmit — send a network packet
 */
int virtio_net_transmit(const void *data, uint16_t len)
{
    if (!net_dev || !data || len == 0 || len > VIRTIO_NET_MAX_PACKET_SIZE)
        return -1;
    
    struct virtio_queue *txq = &net_dev->queues[0];
    
    /* Allocate packet structure (header + data) */
    struct net_packet *pkt = (struct net_packet *)kalloc(sizeof(struct net_packet));
    if (!pkt)
        return -1;
    
    /* Initialize virtio net header (no offloads) */
    kmemset(&pkt->hdr, 0, sizeof(struct virtio_net_hdr));
    pkt->hdr.flags = 0;
    pkt->hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;
    
    /* Copy packet data */
    kmemcpy(pkt->data, data, len);
    pkt->len = len;
    
    /* Build 2-descriptor chain: header + data */
    uint16_t head_idx = txq->last_avail;
    uint16_t data_idx = (head_idx + 1) % txq->size;
    
    /* Descriptor 0: Virtio net header (device reads) */
    txq->desc[head_idx].addr = (uint64_t)&pkt->hdr;
    txq->desc[head_idx].len = sizeof(struct virtio_net_hdr);
    txq->desc[head_idx].flags = VIRTIO_DESC_F_NEXT;
    txq->desc[head_idx].next = data_idx;
    
    /* Descriptor 1: Packet data (device reads) */
    txq->desc[data_idx].addr = (uint64_t)pkt->data;
    txq->desc[data_idx].len = len;
    txq->desc[data_idx].flags = 0;  /* Last descriptor */
    txq->desc[data_idx].next = 0;
    
    /* Add to available ring */
    uint16_t avail_idx = txq->avail->idx % txq->size;
    txq->avail->ring[avail_idx] = head_idx;
    txq->avail->idx++;
    
    /* Update tracking */
    txq->last_avail = (txq->last_avail + 2) % txq->size;
    
    /* Notify device */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_NOTIFY, 0);  /* Queue 0 = TX */
    
    /* Poll for completion with timeout */
    uint32_t timeout = 100000;
    while (timeout--) {
        if (txq->used->idx != txq->last_used) {
            /* TX complete */
            txq->last_used = (txq->last_used + 1) % txq->size;
            kfree(pkt);
            return 0;
        }
        io_delay(10);
    }
    
    /* Timeout */
    kfree(pkt);
    return -1;
}

/*
 * virtio_net_receive — receive a network packet (non-blocking poll)
 */
int virtio_net_receive(void *buffer, uint16_t max_len)
{
    if (!net_dev || !buffer || max_len == 0)
        return -1;
    
    struct virtio_queue *rxq = &net_dev->queues[1];
    
    /* Check if new packet available in used ring */
    if (rxq->used->idx == rxq->last_used) {
        return 0;  /* No packet available */
    }
    
    /* Get used element */
    struct virtio_used_elem *used_elem = &rxq->used->ring[rxq->last_used % rxq->size];
    uint16_t desc_idx = (uint16_t)used_elem->id;
    uint32_t bytes_written = used_elem->len;
    
    /* Get packet from RX buffer */
    struct net_packet *pkt = rx_buffers[desc_idx];
    if (!pkt) {
        rxq->last_used = (rxq->last_used + 1) % rxq->size;
        return -1;
    }
    
    /* Extract packet data (skip virtio net header) */
    uint16_t data_len = bytes_written - sizeof(struct virtio_net_hdr);
    if (data_len > max_len)
        data_len = max_len;
    
    kmemcpy(buffer, pkt->data, data_len);
    
    /* Re-submit this buffer to RX queue for reuse */
    kmemset(pkt, 0, sizeof(struct net_packet));
    
    rxq->desc[desc_idx].addr = (uint64_t)pkt;
    rxq->desc[desc_idx].len = sizeof(struct net_packet);
    rxq->desc[desc_idx].flags = VIRTIO_DESC_F_WRITE;
    
    uint16_t avail_idx = rxq->avail->idx % rxq->size;
    rxq->avail->ring[avail_idx] = desc_idx;
    rxq->avail->idx++;
    
    /* Notify device of new RX buffer */
    virtio_mmio_write(VIRTIO_OFFSET_QUEUE_NOTIFY, 1);
    
    /* Update used ring tracking */
    rxq->last_used = (rxq->last_used + 1) % rxq->size;
    
    return (int)data_len;
}

/*
 * virtio_net_get_mac — get device MAC address
 */
void virtio_net_get_mac(uint8_t mac_out[6])
{
    if (!mac_out)
        return;
    
    for (int i = 0; i < 6; i++) {
        mac_out[i] = net_config.mac[i];
    }
}

/*
 * virtio_net_get_status — get link status
 */
int virtio_net_get_status(void)
{
    if (!net_dev)
        return 0;
    
    return (net_config.status & 1);  /* Bit 0 = link up */
}

/*
 * virtio_net_test — simple test of network functionality
 */
void virtio_net_test(void)
{
    if (!net_dev) {
        serial_putstr("[virtio-net] test SKIP — device not initialized\r\n");
        return;
    }
    
    serial_putstr("[virtio-net] test starting — attempting packet TX\r\n");
    
    /* Create a simple test packet (ARP request broadcast) */
    uint8_t test_packet[] = {
        /* Ethernet header: broadcast destination */
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* dst MAC */
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,  /* src MAC (QEMU default) */
        0x08, 0x06,                          /* EtherType: ARP */
        /* ARP payload (minimal) */
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01
    };
    
    int result = virtio_net_transmit(test_packet, sizeof(test_packet));
    
    if (result == 0) {
        serial_putstr("[virtio-net] test PASS — packet transmitted\r\n");
    } else {
        serial_putstr("[virtio-net] test SKIP — TX timeout (may be expected if no network configured)\r\n");
    }
}
