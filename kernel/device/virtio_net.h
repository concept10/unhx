/*
 * kernel/device/virtio_net.h — Virtio network device driver
 *
 * Implements network packet transmission and reception for QEMU virtio-net.
 */

#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "virtio.h"
#include <stdint.h>

/* Virtio Network Device Feature Bits */
#define VIRTIO_NET_F_CSUM              0   /* Host handles partial checksums */
#define VIRTIO_NET_F_GUEST_CSUM        1   /* Guest handles partial checksums */
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2 /* Control channel offloads */
#define VIRTIO_NET_F_MTU               3   /* Initial MTU advice */
#define VIRTIO_NET_F_MAC               5   /* Host has given MAC address */
#define VIRTIO_NET_F_GUEST_TSO4        7   /* Guest can receive TSOv4 */
#define VIRTIO_NET_F_GUEST_TSO6        8   /* Guest can receive TSOv6 */
#define VIRTIO_NET_F_GUEST_ECN         9   /* Guest can receive TSO with ECN */
#define VIRTIO_NET_F_GUEST_UFO         10  /* Guest can receive UFO */
#define VIRTIO_NET_F_HOST_TSO4         11  /* Host can receive TSOv4 */
#define VIRTIO_NET_F_HOST_TSO6         12  /* Host can receive TSOv6 */
#define VIRTIO_NET_F_HOST_ECN          13  /* Host can receive TSO with ECN */
#define VIRTIO_NET_F_HOST_UFO          14  /* Host can receive UFO */
#define VIRTIO_NET_F_MRG_RXBUF         15  /* Host can merge receive buffers */
#define VIRTIO_NET_F_STATUS            16  /* Configuration status field available */
#define VIRTIO_NET_F_CTRL_VQ           17  /* Control channel available */
#define VIRTIO_NET_F_CTRL_RX           18  /* Control channel RX mode support */
#define VIRTIO_NET_F_CTRL_VLAN         19  /* Control channel VLAN filtering */
#define VIRTIO_NET_F_GUEST_ANNOUNCE    21  /* Guest can send gratuitous packets */
#define VIRTIO_NET_F_MQ                22  /* Device supports multiqueue with auto receive steering */
#define VIRTIO_NET_F_CTRL_MAC_ADDR     23  /* Set MAC address through control channel */

/* Network Device Configuration Space */
struct virtio_net_config {
    uint8_t mac[6];          /* MAC address */
    uint16_t status;         /* Device status */
    uint16_t max_virtqueue_pairs; /* Maximum number of queue pairs */
    uint16_t mtu;            /* Maximum transmission unit */
} __attribute__((packed));

/* Virtio Network Packet Header */
struct virtio_net_hdr {
    uint8_t flags;           /* Flags */
    uint8_t gso_type;        /* GSO type */
    uint16_t hdr_len;        /* Header length */
    uint16_t gso_size;       /* GSO size */
    uint16_t csum_start;     /* Checksum start */
    uint16_t csum_offset;    /* Checksum offset */
    uint16_t num_buffers;    /* Number of buffers (for mergeable RX buffers) */
} __attribute__((packed));

/* Virtio Net Header Flags */
#define VIRTIO_NET_HDR_F_NEEDS_CSUM    1
#define VIRTIO_NET_HDR_F_DATA_VALID    2

/* GSO Types */
#define VIRTIO_NET_HDR_GSO_NONE        0
#define VIRTIO_NET_HDR_GSO_TCPV4       1
#define VIRTIO_NET_HDR_GSO_UDP         3
#define VIRTIO_NET_HDR_GSO_TCPV6       4
#define VIRTIO_NET_HDR_GSO_ECN         0x80

/* Maximum packet size (MTU + headers) */
#define VIRTIO_NET_MAX_PACKET_SIZE     1514  /* Standard Ethernet MTU */
#define VIRTIO_NET_RX_BUFFERS          16    /* Number of pre-allocated RX buffers */

/* Network packet structure */
struct net_packet {
    struct virtio_net_hdr hdr;
    uint8_t data[VIRTIO_NET_MAX_PACKET_SIZE];
    uint16_t len;  /* Actual data length */
};

/* Initialize virtio-net driver */
void virtio_net_init(void);

/* Test network functionality (sends test packet) */
void virtio_net_test(void);

/*
 * virtio_net_transmit — send a network packet
 *
 * data — packet data (Ethernet frame)
 * len — packet length in bytes
 *
 * Returns 0 on success, -1 on error.
 */
int virtio_net_transmit(const void *data, uint16_t len);

/*
 * virtio_net_receive — receive a network packet (non-blocking)
 *
 * buffer — destination buffer for packet data
 * max_len — maximum bytes to receive
 *
 * Returns number of bytes received, 0 if no packet available, -1 on error.
 */
int virtio_net_receive(void *buffer, uint16_t max_len);

/*
 * virtio_net_get_mac — get device MAC address
 *
 * mac_out — 6-byte buffer for MAC address
 */
void virtio_net_get_mac(uint8_t mac_out[6]);

/*
 * virtio_net_get_status — get link status
 *
 * Returns 1 if link up, 0 if link down.
 */
int virtio_net_get_status(void);

#endif /* VIRTIO_NET_H */
