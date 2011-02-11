/*
 * Copyright (c) 2010 Lenka Trochtova
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup pciintel
 * @{
 */
/** @file
 */

#ifndef PCI_H_
#define PCI_H_

#include <stdlib.h>
#include <driver.h>
#include <malloc.h>

#include "pci_regs.h"

#define PCI_MAX_HW_RES 8

typedef struct pci_fun_data {
	int bus;
	int dev;
	int fn;
	int vendor_id;
	int device_id;
	hw_resource_list_t hw_resources;
} pci_fun_data_t;

extern void create_pci_match_ids(function_t *);

extern uint8_t pci_conf_read_8(function_t *, int);
extern uint16_t pci_conf_read_16(function_t *, int);
extern uint32_t pci_conf_read_32(function_t *, int);
extern void pci_conf_write_8(function_t *, int, uint8_t);
extern void pci_conf_write_16(function_t *, int, uint16_t);
extern void pci_conf_write_32(function_t *, int, uint32_t);

extern void pci_add_range(function_t *, uint64_t, size_t, bool);
extern int pci_read_bar(function_t *, int);
extern void pci_read_interrupt(function_t *);
extern void pci_add_interrupt(function_t *, int);

extern void pci_bus_scan(device_t *, int);

extern pci_fun_data_t *create_pci_fun_data(void);
extern void init_pci_fun_data(pci_fun_data_t *, int, int, int);
extern void delete_pci_fun_data(pci_fun_data_t *);
extern void create_pci_fun_name(function_t *);

extern bool pci_alloc_resource_list(function_t *);
extern void pci_clean_resource_list(function_t *);

extern void pci_read_bars(function_t *);
extern size_t pci_bar_mask_to_size(uint32_t);

#endif

/**
 * @}
 */