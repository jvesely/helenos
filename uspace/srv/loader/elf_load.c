/*
 * Copyright (c) 2006 Sergey Bondari
 * Copyright (c) 2006 Jakub Jermar
 * Copyright (c) 2008 Jiri Svoboda
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

/** @addtogroup generic	
 * @{
 */

/**
 * @file
 * @brief	Userspace ELF loader.
 *
 * This module allows loading ELF binaries (both executables and
 * shared objects) from VFS. The current implementation allocates
 * anonymous memory, fills it with segment data and then adjusts
 * the memory areas' flags to the final value. In the future,
 * the segments will be mapped directly from the file.
 */

#include <stdio.h>
#include <sys/types.h>
#include <align.h>
#include <assert.h>
#include <as.h>
#include <unistd.h>
#include <fcntl.h>
#include <smc.h>
#include <loader/pcb.h>

#include "elf.h"
#include "elf_load.h"
#include "arch.h"

#define DPRINTF(...)

static char *error_codes[] = {
	"no error",
	"invalid image",
	"address space error",
	"incompatible image",
	"unsupported image type",
	"irrecoverable error"
};

static unsigned int elf_load(elf_ld_t *elf, size_t so_bias);
static int segment_header(elf_ld_t *elf, elf_segment_header_t *entry);
static int section_header(elf_ld_t *elf, elf_section_header_t *entry);
static int load_segment(elf_ld_t *elf, elf_segment_header_t *entry);

/** Read until the buffer is read in its entirety. */
static int my_read(int fd, void *buf, size_t len)
{
	int cnt = 0;
	do {
		buf += cnt;
		len -= cnt;
		cnt = read(fd, buf, len);
	} while ((cnt > 0) && ((len - cnt) > 0));

	return cnt;
}

/** Load ELF binary from a file.
 *
 * Load an ELF binary from the specified file. If the file is
 * an executable program, it is loaded unbiased. If it is a shared
 * object, it is loaded with the bias @a so_bias. Some information
 * extracted from the binary is stored in a elf_info_t structure
 * pointed to by @a info.
 *
 * @param file_name	Path to the ELF file.
 * @param so_bias	Bias to use if the file is a shared object.
 * @param info		Pointer to a structure for storing information
 *			extracted from the binary.
 *
 * @return EOK on success or negative error code.
 */
int elf_load_file(char *file_name, size_t so_bias, elf_info_t *info)
{
	elf_ld_t elf;

	int fd;
	int rc;

	fd = open(file_name, O_RDONLY);
	if (fd < 0) {
		DPRINTF("failed opening file\n");
		return -1;
	}

	elf.fd = fd;
	elf.info = info;

	rc = elf_load(&elf, so_bias);

	close(fd);

	return rc;
}

/** Run an ELF executable.
 *
 * Transfers control to the entry point of an ELF executable loaded
 * earlier with elf_load_file(). This function does not return.
 *
 * @param info	Info structure filled earlier by elf_load_file()
 */
void elf_run(elf_info_t *info, pcb_t *pcb)
{
	program_run(info->entry, pcb);

	/* not reached */
}

/** Create the program control block (PCB).
 *
 * Fills the program control block @a pcb with information from
 * @a info.
 *
 * @param info	Program info structure
 * @return EOK on success or negative error code
 */
void elf_create_pcb(elf_info_t *info, pcb_t *pcb)
{
	pcb->entry = info->entry;
	pcb->dynamic = info->dynamic;
}


/** Load an ELF binary.
 *
 * The @a elf structure contains the loader state, including
 * an open file, from which the binary will be loaded,
 * a pointer to the @c info structure etc.
 *
 * @param elf		Pointer to loader state buffer.
 * @param so_bias	Bias to use if the file is a shared object.
 * @return EE_OK on success or EE_xx error code.
 */
static unsigned int elf_load(elf_ld_t *elf, size_t so_bias)
{
	elf_header_t header_buf;
	elf_header_t *header = &header_buf;
	int i, rc;

	rc = my_read(elf->fd, header, sizeof(elf_header_t));
	if (rc < 0) {
		DPRINTF("Read error.\n"); 
		return EE_INVALID;
	}

	elf->header = header;

	/* Identify ELF */
	if (header->e_ident[EI_MAG0] != ELFMAG0 ||
	    header->e_ident[EI_MAG1] != ELFMAG1 || 
	    header->e_ident[EI_MAG2] != ELFMAG2 ||
	    header->e_ident[EI_MAG3] != ELFMAG3) {
		DPRINTF("Invalid header.\n");
		return EE_INVALID;
	}
	
	/* Identify ELF compatibility */
	if (header->e_ident[EI_DATA] != ELF_DATA_ENCODING ||
	    header->e_machine != ELF_MACHINE || 
	    header->e_ident[EI_VERSION] != EV_CURRENT ||
	    header->e_version != EV_CURRENT ||
	    header->e_ident[EI_CLASS] != ELF_CLASS) {
		DPRINTF("Incompatible data/version/class.\n");
		return EE_INCOMPATIBLE;
	}

	if (header->e_phentsize != sizeof(elf_segment_header_t)) {
		DPRINTF("e_phentsize:%d != %d\n", header->e_phentsize,
		    sizeof(elf_segment_header_t));
		return EE_INCOMPATIBLE;
	}

	if (header->e_shentsize != sizeof(elf_section_header_t)) {
		DPRINTF("e_shentsize:%d != %d\n", header->e_shentsize,
		    sizeof(elf_section_header_t));
		return EE_INCOMPATIBLE;
	}

	/* Check if the object type is supported. */
	if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
		DPRINTF("Object type %d is not supported\n", header->e_type);
		return EE_UNSUPPORTED;
	}

	/* Shared objects can be loaded with a bias */
	if (header->e_type == ET_DYN)
		elf->bias = so_bias;
	else
		elf->bias = 0;

	elf->info->interp = NULL;
	elf->info->dynamic = NULL;

	/* Walk through all segment headers and process them. */
	for (i = 0; i < header->e_phnum; i++) {
		elf_segment_header_t segment_hdr;

		/* Seek to start of segment header */
		lseek(elf->fd, header->e_phoff
		        + i * sizeof(elf_segment_header_t), SEEK_SET);

		rc = my_read(elf->fd, &segment_hdr,
		    sizeof(elf_segment_header_t));
		if (rc < 0) {
			DPRINTF("Read error.\n");
			return EE_INVALID;
		}

		rc = segment_header(elf, &segment_hdr);
		if (rc != EE_OK)
			return rc;
	}

	DPRINTF("Parse sections.\n");

	/* Inspect all section headers and proccess them. */
	for (i = 0; i < header->e_shnum; i++) {
		elf_section_header_t section_hdr;

		/* Seek to start of section header */
		lseek(elf->fd, header->e_shoff
		    + i * sizeof(elf_section_header_t), SEEK_SET);

		rc = my_read(elf->fd, &section_hdr,
		    sizeof(elf_section_header_t));
		if (rc < 0) {
			DPRINTF("Read error.\n");
			return EE_INVALID;
		}

		rc = section_header(elf, &section_hdr);
		if (rc != EE_OK)
			return rc;
	}

	elf->info->entry =
	    (entry_point_t)((uint8_t *)header->e_entry + elf->bias);

	DPRINTF("Done.\n");

	return EE_OK;
}

/** Print error message according to error code.
 *
 * @param rc Return code returned by elf_load().
 *
 * @return NULL terminated description of error.
 */
char *elf_error(unsigned int rc)
{
	assert(rc < sizeof(error_codes) / sizeof(char *));

	return error_codes[rc];
}

/** Process segment header.
 *
 * @param entry	Segment header.
 *
 * @return EE_OK on success, error code otherwise.
 */
static int segment_header(elf_ld_t *elf, elf_segment_header_t *entry)
{
	switch (entry->p_type) {
	case PT_NULL:
	case PT_PHDR:
		break;
	case PT_LOAD:
		return load_segment(elf, entry);
		break;
	case PT_INTERP:
		/* Assume silently interp == "/rtld.so" */
		elf->info->interp = "/rtld.so";
		break;
	case PT_DYNAMIC:
	case PT_SHLIB:
	case PT_NOTE:
	case PT_LOPROC:
	case PT_HIPROC:
	default:
		DPRINTF("Segment p_type %d unknown.\n", entry->p_type);
		return EE_UNSUPPORTED;
		break;
	}
	return EE_OK;
}

/** Load segment described by program header entry.
 *
 * @param elf	Loader state.
 * @param entry Program header entry describing segment to be loaded.
 *
 * @return EE_OK on success, error code otherwise.
 */
int load_segment(elf_ld_t *elf, elf_segment_header_t *entry)
{
	void *a;
	int flags = 0;
	uintptr_t bias;
	uintptr_t base;
	void *seg_ptr;
	uintptr_t seg_addr;
	size_t mem_sz;
	int rc;

	bias = elf->bias;

	seg_addr = entry->p_vaddr + bias;
	seg_ptr = (void *) seg_addr;

	DPRINTF("Load segment at addr %p, size 0x%x\n", seg_addr,
		entry->p_memsz);

	if (entry->p_align > 1) {
		if ((entry->p_offset % entry->p_align) !=
		    (seg_addr % entry->p_align)) {
			DPRINTF("Align check 1 failed offset%%align=%d, "
			    "vaddr%%align=%d\n",
			    entry->p_offset % entry->p_align,
			    seg_addr % entry->p_align
			);
			return EE_INVALID;
		}
	}

	/* Final flags that will be set for the memory area */

	if (entry->p_flags & PF_X)
		flags |= AS_AREA_EXEC;
	if (entry->p_flags & PF_W)
		flags |= AS_AREA_WRITE;
	if (entry->p_flags & PF_R)
		flags |= AS_AREA_READ;
	flags |= AS_AREA_CACHEABLE;
	
	base = ALIGN_DOWN(entry->p_vaddr, PAGE_SIZE);
	mem_sz = entry->p_memsz + (entry->p_vaddr - base);

	DPRINTF("Map to seg_addr=%p-%p.\n", seg_addr,
	entry->p_vaddr + bias + ALIGN_UP(entry->p_memsz, PAGE_SIZE));

	/*
	 * For the course of loading, the area needs to be readable
	 * and writeable.
	 */
	a = as_area_create((uint8_t *)base + bias, mem_sz,
	    AS_AREA_READ | AS_AREA_WRITE | AS_AREA_CACHEABLE);
	if (a == (void *)(-1)) {
		DPRINTF("Memory mapping failed.\n");
		return EE_MEMORY;
	}

	DPRINTF("as_area_create(%p, 0x%x, %d) -> 0x%lx\n",
		base + bias, mem_sz, flags, (uintptr_t)a);

	/*
	 * Load segment data
	 */
	rc = lseek(elf->fd, entry->p_offset, SEEK_SET);
	if (rc < 0) {
		printf("seek error\n");
		return EE_INVALID;
	}

/*	rc = read(fd, (void *)(entry->p_vaddr + bias), entry->p_filesz);
	if (rc < 0) { printf("read error\n"); return EE_INVALID; }*/

	/* Long reads are not possible yet. Load segment piecewise. */

	unsigned left, now;
	uint8_t *dp;

	left = entry->p_filesz;
	dp = seg_ptr;

	while (left > 0) {
		now = 16384;
		if (now > left) now = left;

		rc = my_read(elf->fd, dp, now);

		if (rc < 0) { 
			DPRINTF("Read error.\n");
			return EE_INVALID;
		}

		left -= now;
		dp += now;
	}

	rc = as_area_change_flags(seg_ptr, flags);
	if (rc != 0) {
		DPRINTF("Failed to set memory area flags.\n");
		return EE_MEMORY;
	}

	if (flags & AS_AREA_EXEC) {
		/* Enforce SMC coherence for the segment */
		if (smc_coherence(seg_ptr, entry->p_filesz))
			return EE_MEMORY;
	}

	return EE_OK;
}

/** Process section header.
 *
 * @param elf	Loader state.
 * @param entry Segment header.
 *
 * @return EE_OK on success, error code otherwise.
 */
static int section_header(elf_ld_t *elf, elf_section_header_t *entry)
{
	switch (entry->sh_type) {
	case SHT_PROGBITS:
		if (entry->sh_flags & SHF_TLS) {
			/* .tdata */
		}
		break;
	case SHT_NOBITS:
		if (entry->sh_flags & SHF_TLS) {
			/* .tbss */
		}
		break;
	case SHT_DYNAMIC:
		/* Record pointer to dynamic section into info structure */
		elf->info->dynamic =
		    (void *)((uint8_t *)entry->sh_addr + elf->bias);
		DPRINTF("Dynamic section found at %p.\n",
			(uintptr_t)elf->info->dynamic);
		break;
	default:
		break;
	}
	
	return EE_OK;
}

/** @}
 */
