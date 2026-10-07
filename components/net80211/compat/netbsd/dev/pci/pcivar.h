/*
 * @file
 * @brief PCI autoconf shell for the imported drivers. The types mirror
 * NetBSD dev/pci/pcivar.h; the backend implements the calls over its
 * environment PCI API. A pcitag_t is the environment device handle.
 */

#ifndef _COMPAT_DEV_PCI_PCIVAR_H_
#define _COMPAT_DEV_PCI_PCIVAR_H_

#include <sys/cdefs.h>
#include <sys/bus.h>
#include <dev/pci/pcireg.h>

typedef void *pci_chipset_tag_t;
typedef void *pcitag_t;

enum pci_intr_type {
	PCI_INTR_TYPE_INTX = 1,
	PCI_INTR_TYPE_MSI,
	PCI_INTR_TYPE_MSIX,
};

struct pci_intr_handle {
	int irq;
	enum pci_intr_type type;
};

typedef struct pci_intr_handle *pci_intr_handle_t;

struct pci_attach_args {
	pci_chipset_tag_t pa_pc;
	pcitag_t pa_tag;
	pcireg_t pa_id;
	pcireg_t pa_class;
	bus_dma_tag_t pa_dmat;
};

struct pci_attach_args;
typedef struct pci_attach_args pci_attach_args_t;

#define PCI_INTRSTR_LEN 64

/* configuration space */
pcireg_t pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg);
void pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg,
	pcireg_t val);

/* capability list walk; returns 1 and stores the offset when found */
int pci_get_capability(pci_chipset_tag_t pc, pcitag_t tag, int cap_id,
	int *offsetp, pcireg_t *valuep);

/* BAR 0 mapping */
pcireg_t pci_mapreg_type(pci_chipset_tag_t pc, pcitag_t tag, int reg);
int pci_mapreg_map(const struct pci_attach_args *pa, int reg,
	pcireg_t type, int busflags, bus_space_tag_t *tagp,
	bus_space_handle_t *handlep, bus_addr_t *basep, bus_size_t *sizep);

/* interrupts: one MSI vector (the drivers here take a single line) */
int pci_intr_alloc(const struct pci_attach_args *pa,
	pci_intr_handle_t **ihpp, int *countsp, int mintype);
enum pci_intr_type pci_intr_type(pci_chipset_tag_t pc,
	pci_intr_handle_t ih);
const char *pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih,
	char *buf, size_t len);
void *pci_intr_establish_xname(pci_chipset_tag_t pc,
	pci_intr_handle_t ih, int level, int (*func)(void *), void *arg,
	const char *name);
void pci_intr_release(pci_chipset_tag_t pc, pci_intr_handle_t *ihp,
	int count);

void pci_aprint_devinfo(const struct pci_attach_args *pa, const char *name);

#endif /* _COMPAT_DEV_PCI_PCIVAR_H_ */
