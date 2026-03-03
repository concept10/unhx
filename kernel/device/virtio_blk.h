/*
 * kernel/device/virtio_blk.h — Virtio block device driver
 *
 * Implements reading and writing QEMU virtio-blk disks.
 */

#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "virtio.h"

/* Initialize virtio-blk driver */
void virtio_blk_init(void);

/* Test disk I/O (reads sector 0) */
void virtio_blk_test(void);

/*
 * virtio_blk_get_capacity — return device capacity in 512-byte sectors
 */
uint64_t virtio_blk_get_capacity(void);

/*
 * virtio_blk_read_sectors — read sectors from block device
 *
 * sector — starting LBA
 * count — number of 512-byte sectors to read
 * buffer — destination buffer (must be physically contiguous)
 *
 * Returns 0 on success, -1 on error.
 */
int virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buffer);

/*
 * virtio_blk_write_sectors — write sectors to block device
 *
 * sector — starting LBA
 * count — number of 512-byte sectors to write
 * buffer — source buffer (must be physically contiguous)
 *
 * Returns 0 on success, -1 on error.
 */
int virtio_blk_write_sectors(uint64_t sector, uint32_t count, const void *buffer);

/*
 * virtio_blk_get_capacity — get device capacity in sectors
 */
uint64_t virtio_blk_get_capacity(void);

#endif /* VIRTIO_BLK_H */
