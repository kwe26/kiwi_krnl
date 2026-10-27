/* PCI configuration space (mechanism #1) and a boot-time device list. */
#include "kernel.h"

struct kiwi_pcidev pci_devs[PCI_MAX];
int pci_ndev;

uint32_t pci_read(int bus, int dev, int fn, int off)
{
    outl(0xCF8, 0x80000000u | (uint32_t)bus << 16 | (uint32_t)dev << 11 |
                (uint32_t)fn << 8 | (uint32_t)(off & 0xFC));
    return inl(0xCFC);
}

static void probe(int bus, int dev, int fn)
{
    uint32_t id = pci_read(bus, dev, fn, 0);
    if ((id & 0xFFFF) == 0xFFFF || pci_ndev >= PCI_MAX)
        return;
    struct kiwi_pcidev *d = &pci_devs[pci_ndev++];
    uint32_t cls = pci_read(bus, dev, fn, 8);
    d->bus = (uint8_t)bus;
    d->dev = (uint8_t)dev;
    d->fn = (uint8_t)fn;
    d->vendor = (uint16_t)id;
    d->device = (uint16_t)(id >> 16);
    d->rev = (uint8_t)cls;
    d->progif = (uint8_t)(cls >> 8);
    d->subclass = (uint8_t)(cls >> 16);
    d->cls = (uint8_t)(cls >> 24);
    d->irq = (uint8_t)pci_read(bus, dev, fn, 0x3C);
    for (int i = 0; i < 6; i++)
        d->bar[i] = pci_read(bus, dev, fn, 0x10 + 4 * i);
    kprintf("pci: %d:%d.%d %x:%x class %x.%x irq %d\n", bus, dev, fn,
            d->vendor, d->device, d->cls, d->subclass, d->irq);
}

void pci_init(void)
{
    for (int bus = 0; bus < 8; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            if ((pci_read(bus, dev, 0, 0) & 0xFFFF) == 0xFFFF)
                continue;
            probe(bus, dev, 0);
            if (pci_read(bus, dev, 0, 0x0C) & 0x00800000)      /* multi-function */
                for (int fn = 1; fn < 8; fn++)
                    probe(bus, dev, fn);
        }
    }
}

struct kiwi_pcidev *pci_find(uint16_t vendor, uint16_t device)
{
    for (int i = 0; i < pci_ndev; i++)
        if (pci_devs[i].vendor == vendor && pci_devs[i].device == device)
            return &pci_devs[i];
    return 0;
}
