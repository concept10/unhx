/*
 * kernel/device/ahci.h — AHCI controller probe for UNHOX
 */

#ifndef AHCI_H
#define AHCI_H

/* Detect and probe AHCI controllers present on PCI. */
void ahci_init(void);

#endif /* AHCI_H */
