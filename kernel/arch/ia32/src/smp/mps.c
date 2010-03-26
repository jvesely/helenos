/*
 * Copyright (c) 2008 Jakub Jermar
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

/** @addtogroup ia32	
 * @{
 */
/** @file
 */

#ifdef CONFIG_SMP

#include <config.h>
#include <print.h>
#include <debug.h>
#include <arch/smp/mps.h>
#include <arch/smp/apic.h>
#include <arch/smp/smp.h>
#include <func.h>
#include <typedefs.h>
#include <cpu.h>
#include <arch/asm.h>
#include <arch/bios/bios.h>
#include <mm/frame.h>

/*
 * MultiProcessor Specification detection code.
 */

#define	FS_SIGNATURE	0x5f504d5f
#define CT_SIGNATURE 	0x504d4350

static int mps_fs_check(uint8_t *base);
static int mps_ct_check(void);

static int configure_via_ct(void);
static int configure_via_default(uint8_t n);

static int ct_processor_entry(struct __processor_entry *pr);
static void ct_bus_entry(struct __bus_entry *bus);
static void ct_io_apic_entry(struct __io_apic_entry *ioa);
static void ct_io_intr_entry(struct __io_intr_entry *iointr);
static void ct_l_intr_entry(struct __l_intr_entry *lintr);

static void ct_extended_entries(void);

static struct mps_fs *fs;
static struct mps_ct *ct;

struct __processor_entry *processor_entries = NULL;
struct __bus_entry *bus_entries = NULL;
struct __io_apic_entry *io_apic_entries = NULL;
struct __io_intr_entry *io_intr_entries = NULL;
struct __l_intr_entry *l_intr_entries = NULL;

unsigned int processor_entry_cnt = 0;
unsigned int bus_entry_cnt = 0;
unsigned int io_apic_entry_cnt = 0;
unsigned int io_intr_entry_cnt = 0;
unsigned int l_intr_entry_cnt = 0;

/*
 * Implementation of IA-32 SMP configuration interface.
 */
static size_t get_cpu_count(void);
static bool is_cpu_enabled(size_t i);
static bool is_bsp(size_t i);
static uint8_t get_cpu_apic_id(size_t i);
static int mps_irq_to_pin(unsigned int irq);

struct smp_config_operations mps_config_operations = {
	.cpu_count = get_cpu_count,
	.cpu_enabled = is_cpu_enabled,
	.cpu_bootstrap = is_bsp,
	.cpu_apic_id = get_cpu_apic_id,
	.irq_to_pin = mps_irq_to_pin
};

size_t get_cpu_count(void)
{
	return processor_entry_cnt;
}

bool is_cpu_enabled(size_t i)
{
	ASSERT(i < processor_entry_cnt);
	return (bool) ((processor_entries[i].cpu_flags & 0x01) == 0x01);
}

bool is_bsp(size_t i)
{
	ASSERT(i < processor_entry_cnt);
	return (bool) ((processor_entries[i].cpu_flags & 0x02) == 0x02);
}

uint8_t get_cpu_apic_id(size_t i)
{
	ASSERT(i < processor_entry_cnt);
	return processor_entries[i].l_apic_id;
}


/*
 * Used to check the integrity of the MP Floating Structure.
 */
int mps_fs_check(uint8_t *base)
{
	unsigned int i;
	uint8_t sum;
	
	for (i = 0, sum = 0; i < 16; i++)
		sum = (uint8_t) (sum + base[i]);
	
	return !sum;
}

/*
 * Used to check the integrity of the MP Configuration Table.
 */
int mps_ct_check(void)
{
	uint8_t *base = (uint8_t *) ct;
	uint8_t *ext = base + ct->base_table_length;
	uint8_t sum;
	int i;	
	
	/* count the checksum for the base table */
	for (i = 0,sum = 0; i < ct->base_table_length; i++)
		sum = (uint8_t) (sum + base[i]);
		
	if (sum)
		return 0;
		
	/* count the checksum for the extended table */
	for (i = 0, sum = 0; i < ct->ext_table_length; i++)
		sum = (uint8_t) (sum + ext[i]);
		
	return sum == ct->ext_table_checksum;
}

void mps_init(void)
{
	uint8_t *addr[2] = { NULL, (uint8_t *) PA2KA(0xf0000) };
	unsigned int i, j, length[2] = { 1024, 64 * 1024 };
	

	/*
	 * Find MP Floating Pointer Structure
	 * 1a. search first 1K of EBDA
	 * 1b. if EBDA is undefined, search last 1K of base memory
	 *  2. search 64K starting at 0xf0000
	 */

	addr[0] = (uint8_t *) PA2KA(ebda ? ebda : 639 * 1024);
	for (i = 0; i < 2; i++) {
		for (j = 0; j < length[i]; j += 16) {
			if (*((uint32_t *) &addr[i][j]) ==
			    FS_SIGNATURE && mps_fs_check(&addr[i][j])) {
				fs = (struct mps_fs *) &addr[i][j];
				goto fs_found;
			}
		}
	}

	return;
	
fs_found:
	printf("%p: MPS Floating Pointer Structure\n", fs);

	if (fs->config_type == 0 && fs->configuration_table) {
		if (fs->mpfib2 >> 7) {
			printf("%s: PIC mode not supported\n", __func__);
			return;
		}

		ct = (struct mps_ct *)PA2KA((uintptr_t)fs->configuration_table);
		config.cpu_count = configure_via_ct();
	} 
	else
		config.cpu_count = configure_via_default(fs->config_type);

	return;
}

int configure_via_ct(void)
{
	uint8_t *cur;
	unsigned int i, cnt;
		
	if (ct->signature != CT_SIGNATURE) {
		printf("%s: bad ct->signature\n", __func__);
		return 1;
	}
	if (!mps_ct_check()) {
		printf("%s: bad ct checksum\n", __func__);
		return 1;
	}
	if (ct->oem_table) {
		printf("%s: ct->oem_table not supported\n", __func__);
		return 1;
	}
	
	l_apic = (uint32_t *)(uintptr_t)ct->l_apic;

	cnt = 0;
	cur = &ct->base_table[0];
	for (i = 0; i < ct->entry_count; i++) {
		switch (*cur) {
		/* Processor entry */
		case 0:	
			processor_entries = processor_entries ?
			    processor_entries :
			    (struct __processor_entry *) cur;
			processor_entry_cnt++;
			cnt += ct_processor_entry((struct __processor_entry *)
			    cur);
			cur += 20;
			break;

		/* Bus entry */
		case 1:
			bus_entries = bus_entries ?
			    bus_entries : (struct __bus_entry *) cur;
			bus_entry_cnt++;
			ct_bus_entry((struct __bus_entry *) cur);
			cur += 8;
			break;
				
		/* I/O Apic */
		case 2:
			io_apic_entries = io_apic_entries ?
			    io_apic_entries : (struct __io_apic_entry *) cur;
				io_apic_entry_cnt++;
			ct_io_apic_entry((struct __io_apic_entry *) cur);
			cur += 8;
			break;
				
		/* I/O Interrupt Assignment */
		case 3:
			io_intr_entries = io_intr_entries ?
			    io_intr_entries : (struct __io_intr_entry *) cur;
			io_intr_entry_cnt++;
			ct_io_intr_entry((struct __io_intr_entry *) cur);
			cur += 8;
			break;

		/* Local Interrupt Assignment */
		case 4:
			l_intr_entries = l_intr_entries ?
			    l_intr_entries : (struct __l_intr_entry *) cur;
			l_intr_entry_cnt++;
			ct_l_intr_entry((struct __l_intr_entry *) cur);
			cur += 8;
			break;

		default:
			/*
			 * Something is wrong. Fallback to UP mode.
			 */

			printf("%s: ct badness\n", __func__);
			return 1;
		}
	}
	
	/*
	 * Process extended entries.
	 */
	ct_extended_entries();
	return cnt;
}

int configure_via_default(uint8_t n __attribute__((unused)))
{
	/*
	 * Not yet implemented.
	 */
	printf("%s: not supported\n", __func__);
	return 1;
}


int ct_processor_entry(struct __processor_entry *pr __attribute__((unused)))
{
	/*
	 * Ignore processors which are not marked enabled.
	 */
	if ((pr->cpu_flags & (1 << 0)) == 0)
		return 0;
	
	apic_id_mask |= (1 << pr->l_apic_id); 
	return 1;
}

void ct_bus_entry(struct __bus_entry *bus __attribute__((unused)))
{
#ifdef MPSCT_VERBOSE
	char buf[7];
	memcpy((void *) buf, (void *) bus->bus_type, 6);
	buf[6] = 0;
	printf("bus%d: %s\n", bus->bus_id, buf);
#endif
}

void ct_io_apic_entry(struct __io_apic_entry *ioa)
{
	static unsigned int io_apic_count = 0;

	/* this ioapic is marked unusable */
	if ((ioa->io_apic_flags & 1) == 0)
		return;
	
	if (io_apic_count++ > 0) {
		/*
		 * Multiple IO APIC's are currently not supported.
		 */
		return;
	}
	
	io_apic = (uint32_t *)(uintptr_t)ioa->io_apic;
}

//#define MPSCT_VERBOSE
void ct_io_intr_entry(struct __io_intr_entry *iointr __attribute__((unused)))
{
#ifdef MPSCT_VERBOSE
	switch (iointr->intr_type) {
	case 0:
		printf("INT");
		break;
	case 1:
		printf("NMI");
		break;
	case 2:
		printf("SMI");
		break;
	case 3:
		printf("ExtINT");
		break;
	}
	putchar(',');
	switch (iointr->poel & 3) {
	case 0:
		printf("bus-like");
		break;
	case 1:
		printf("active high");
		break;
	case 2:
		printf("reserved");
		break;
	case 3:
		printf("active low");
		break;
	}
	putchar(',');
	switch ((iointr->poel >> 2) & 3) {
	case 0:
		printf("bus-like");
		break;
	case 1:
		printf("edge-triggered");
		break;
	case 2:
		printf("reserved");
		break;
	case 3:
		printf("level-triggered");
		break;
	}
	putchar(',');
	printf("bus%d,irq%d", iointr->src_bus_id, iointr->src_bus_irq);
	putchar(',');
	printf("io_apic%d,pin%d", iointr->dst_io_apic_id,
	    iointr->dst_io_apic_pin);
	putchar('\n');	
#endif
}

void ct_l_intr_entry(struct __l_intr_entry *lintr __attribute__((unused)))
{
#ifdef MPSCT_VERBOSE
	switch (lintr->intr_type) {
	case 0:
	    printf("INT");
	    break;
	case 1:
	    printf("NMI");
	    break;
	case 2:
	    printf("SMI");
	    break;
	case 3:
	    printf("ExtINT");
	    break;
	}
	putchar(',');
	switch (lintr->poel & 3) {
	case 0:
	    printf("bus-like");
	    break;
	case 1:
	    printf("active high");
	    break;
	case 2:
	    printf("reserved");
	    break;
	case 3:
	    printf("active low");
	    break;
	}
	putchar(',');
	switch ((lintr->poel >> 2) & 3) {
	case 0:
	    printf("bus-like");
	    break;
	case 1:
	    printf("edge-triggered");
	    break;
	case 2:
	    printf("reserved");
	    break;
	case 3:
	    printf("level-triggered");
	    break;
	}
	putchar(',');
	printf("bus%d,irq%d", lintr->src_bus_id, lintr->src_bus_irq);
	putchar(',');
	printf("l_apic%d,pin%d", lintr->dst_l_apic_id, lintr->dst_l_apic_pin);
	putchar('\n');
#endif
}

void ct_extended_entries(void)
{
	uint8_t *ext = (uint8_t *) ct + ct->base_table_length;
	uint8_t *cur;

	for (cur = ext; cur < ext + ct->ext_table_length;
	    cur += cur[CT_EXT_ENTRY_LEN]) {
		switch (cur[CT_EXT_ENTRY_TYPE]) {
		default:
			printf("%p: skipping MP Configuration Table extended "
			    "entry type %d\n", cur, cur[CT_EXT_ENTRY_TYPE]);
			break;
		}
	}
}

int mps_irq_to_pin(unsigned int irq)
{
	unsigned int i;
	
	for (i = 0; i < io_intr_entry_cnt; i++) {
		if (io_intr_entries[i].src_bus_irq == irq &&
		    io_intr_entries[i].intr_type == 0)
			return io_intr_entries[i].dst_io_apic_pin;
	}
	
	return -1;
}

#endif /* CONFIG_SMP */

/** @}
 */
