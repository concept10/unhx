/*
 * kernel/device/virtio.h — Virtio device framework
 *
 * Minimal Virtio 1.0 support for block and network devices.
 * 
 * Reference: Virtio Specification 1.0, https://docs.oasis-open.org/virtio/virtio/v1.0/
 */

#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>
#include "pci.h"

/* Virtio Device Types */
#define VIRTIO_TYPE_NET       0
#define VIRTIO_TYPE_BLOCK     1
#define VIRTIO_TYPE_CONSOLE   3
#define VIRTIO_TYPE_ENTROPY   4
#define VIRTIO_TYPE_9P        9

/* Virtio Configuration Address Space Offsets (Legacy or MMIO) */
#define VIRTIO_OFFSET_HOST_FEATURES       0x00
#define VIRTIO_OFFSET_GUEST_FEATURES      0x04
#define VIRTIO_OFFSET_FEATURE_SEL         0x08
#define VIRTIO_OFFSET_DRIVER_FEATURES     0x04
#define VIRTIO_OFFSET_DRIVER_FEATURES_SEL 0x08
#define VIRTIO_OFFSET_CONFIG_MSIX_VECTOR  0x0C
#define VIRTIO_OFFSET_QUEUE_SEL           0x22
#define VIRTIO_OFFSET_QUEUE_SIZE          0x12
#define VIRTIO_OFFSET_QUEUE_MSIX_VECTOR   0x24
#define VIRTIO_OFFSET_QUEUE_ENABLE        0x28
#define VIRTIO_OFFSET_QUEUE_NOTIFY_OFF    0x2A
#define VIRTIO_OFFSET_QUEUE_NOTIFY        0x50  /* Queue notification register */
#define VIRTIO_OFFSET_QUEUE_DESC          0x40
#define VIRTIO_OFFSET_QUEUE_AVAIL         0x48
#define VIRTIO_OFFSET_QUEUE_USED          0x50
#define VIRTIO_OFFSET_STATUS              0x12
#define VIRTIO_OFFSET_ISR_STATUS          0x13
#define VIRTIO_OFFSET_CONFIG              0x20

/* Virtio Feature Bits (Common to All Devices) */
#define VIRTIO_F_NOTIFY_ON_EMPTY          0
#define VIRTIO_F_ANY_LAYOUT               27
#define VIRTIO_F_RING_INDIRECT_DESC       28
#define VIRTIO_F_RING_EVENT_IDX           29
#define VIRTIO_F_UNUSED                   30
#define VIRTIO_F_VERSION_1                32

/* Virtio Block Device Feature Bits */
#define VIRTIO_BLK_F_SIZE_MAX             1
#define VIRTIO_BLK_F_SEG_MAX              2
#define VIRTIO_BLK_F_GEOMETRY             4
#define VIRTIO_BLK_F_RO                   5
#define VIRTIO_BLK_F_BLK_SIZE             6
#define VIRTIO_BLK_F_FLUSH                9
#define VIRTIO_BLK_F_TOPOLOGY             10
#define VIRTIO_BLK_F_CONFIG_WCE           11
#define VIRTIO_BLK_F_DISCARD              13
#define VIRTIO_BLK_F_WRITE_ZEROES         14

/* Virtio Block Device Request Types */
#define VIRTIO_BLK_T_IN                   0  /* Read */
#define VIRTIO_BLK_T_OUT                  1  /* Write */
#define VIRTIO_BLK_T_FLUSH                4
#define VIRTIO_BLK_T_GET_ID               8

/* Virtio Block Request Status */
#define VIRTIO_BLK_S_OK                   0
#define VIRTIO_BLK_S_IOERR                1
#define VIRTIO_BLK_S_UNSUPP               2

/* Virtio Status Bits */
#define VIRTIO_STATUS_RESET              0
#define VIRTIO_STATUS_ACK                1
#define VIRTIO_STATUS_DRIVER             2
#define VIRTIO_STATUS_DRIVER_OK          4
#define VIRTIO_STATUS_FEATURES_OK        8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED             128

/* Virtual Queue Descriptor */
struct virtio_desc {
    uint64_t addr;           /* Physical address of data */
    uint32_t len;            /* Length of data */
    uint16_t flags;          /* Descriptor flags */
    uint16_t next;           /* Next descriptor in chain (if NEXT flag set) */
};

#define VIRTIO_DESC_F_NEXT    1
#define VIRTIO_DESC_F_WRITE   2
#define VIRTIO_DESC_F_INDIRECT 4

/* Virtual Queue Available Ring */
struct virtio_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[128];  /* Element indices */
    uint16_t used_event; /* Only used if RING_EVENT_IDX negotiated */
};

/* Used Ring Element */
struct virtio_used_elem {
    uint32_t id;             /* Descriptor index */
    uint32_t len;            /* Bytes written */
};

/* Virtual Queue Used Ring */
struct virtio_used {
    uint16_t flags;
    uint16_t idx;
    struct virtio_used_elem ring[128];
    uint16_t avail_event;  /* Only used if RING_EVENT_IDX negotiated */
};

/* Virtio Queue Structure */
struct virtio_queue {
    uint16_t size;         /* Queue size */
    uint16_t last_avail;   /* Last available index we processed */
    uint16_t last_used;    /* Last used index we read */
    
    struct virtio_desc *desc;    /* Descriptor table */
    struct virtio_avail *avail;  /* Available ring */
    struct virtio_used *used;    /* Used ring */
};

/* Generic Virtio Device */
struct virtio_device {
    struct pci_device *pci_dev;
    uint8_t device_type;
    
    /* Memory map addresses for PCI BAR memory */
    uint64_t pci_bar_addr;
    
    uint32_t enabled_features;
    
    struct virtio_queue queues[16];
    uint32_t queue_count;
};

/* Block Device Configuration Space */
struct virtio_block_config {
    uint64_t capacity;        /* Device capacity in 512–byte sectors */
    uint32_t size_max;        /* Maximum request size */
    uint32_t seg_max;         /* Maximum number of segments */
    struct {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;
    uint32_t blk_size;        /* Block size (typically 512) */
    uint8_t physical_blk_exp;
    uint8_t alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    uint8_t wce;
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t write_zeroes_may_unmap;
};

/* Virtio Block Request */
struct virtio_blk_req {
    uint32_t type;           /* VIRTIO_BLK_T_* */
    uint32_t reserved;
    uint64_t sector;
    uint8_t data[];          /* Variable-length request data */
};

#endif /* VIRTIO_H */
