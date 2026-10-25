/*
 * @file   mmu.h
 * @brief  Identity-mapped MMU bring-up (arch interface).
 */

#ifndef FREEWIFICAM_MMU_H
#define FREEWIFICAM_MMU_H

/* Build the page tables, drain the loader's dirty cache lines, and turn on
 * MMU + I/D caches (identity mapping, Normal cacheable image RAM, Device
 * peripherals). Called from start.S before main(); never returns on
 * failure - a bad table faults through the boot vectors. */
void mmu_enable(void);

#endif /* FREEWIFICAM_MMU_H */
