/*
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */

/* file mapping.h
 * generic mapping driver interface
 * (C) Copyright 2000, Hans Lermen, lermen@fgan.de
 */

#ifndef _MAPPING_H_
#define _MAPPING_H_

#include <sys/mman.h>
#include "memory.h"
#include "cpu-emu.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE	4096
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT	12
#endif
#define EMM_PAGE_SIZE	(16*1024)

#define HUGE_PAGE_SIZE ((uintptr_t)(2*1024*1024))
#define HUGE_PAGE_MASK	(~(HUGE_PAGE_SIZE-1))
#define HUGE_PAGE_ALIGN(addr)	(((addr)+HUGE_PAGE_SIZE-1)&HUGE_PAGE_MASK)

#define Q__printf(f,cap,a...) ({\
  Q_printf(f,decode_mapping_cap(cap),##a); \
})

/* used by(where): (e.g. MAPPING_<cfilename>, name of DOSEMU code part */
#define MAPPING_ALL		0x00ffff
#define MAPPING_PROBE		0
#define MAPPING_OTHER		0x000001
#define MAPPING_EMS		0x000002
#define MAPPING_DPMI		0x000004
#define MAPPING_VGAEMU		0x000008
#define MAPPING_VIDEO		0x000010
#define MAPPING_VC		MAPPING_VIDEO
#define MAPPING_HGC		0x000020
#define MAPPING_HMA		0x000040
#define MAPPING_SHARED		0x000080
#define MAPPING_INIT_HWRAM	0x000100
#define MAPPING_INIT_LOWRAM	0x000200
#define MAPPING_EXTMEM		0x000400
#define MAPPING_KVM		0x000800
#define MAPPING_CPUEMU		0x001000

/* usage as: (kind of mapping required) */
#define MAPPING_KMEM		0x010000
#define MAPPING_LOWMEM		0x020000
#define MAPPING_SCRATCH		0x040000
#define MAPPING_SINGLE		0x080000
#define MAPPING_NULL		0x100000
#define MAPPING_NOOVERLAP	0x200000

#ifdef __x86_64__
/* FIXME: JIT should support 64bit mem_base */
#define _MAP_32BIT ((config.cpu_vm_dpmi == CPUVM_NATIVE || \
    IS_EMU_JIT()) ? MAP_32BIT : 0)
#else
#define _MAP_32BIT 0
#endif

typedef int open_mapping_type(int cap);
int open_mapping (int cap);

typedef void close_mapping_type(int cap);
void close_mapping(int cap);

typedef void *alloc_mapping_type(int cap, size_t mapsize, void *target);
void *alloc_mapping (int cap, size_t mapsize);
void *alloc_mapping_huge_page_aligned (int cap, size_t mapsize);

typedef void free_mapping_type(int cap, void *addr, size_t mapsize);
void free_mapping (int cap, void *addr, size_t mapsize);

typedef void *resize_mapping_type(int cap, void *addr, size_t oldsize, size_t newsize);
void *realloc_mapping (int cap, void *addr, size_t oldsize, size_t newsize);

void *mmap_mapping_huge_page_aligned(int cap, size_t mapsize, int protect);
void *mmap_mapping(int cap, void *target, size_t mapsize, int protect);

typedef void *alias_mapping_type(int cap, void *target, size_t mapsize, int protect, void *source);
int alias_mapping(int cap, dosaddr_t targ, size_t mapsize, int protect, void *source);
int alias_mapping_pa(int cap, unsigned addr, size_t mapsize, int protect, void *source);
int unalias_mapping_pa(int cap, unsigned addr, size_t mapsize);
void *alias_mapping_ux(int cap, size_t mapsize, int protect, void *source);

int munmap_mapping(int cap, dosaddr_t targ, size_t mapsize);
int mprotect_mapping(int cap, dosaddr_t targ, size_t mapsize, int protect);
int restore_mapping(int cap, dosaddr_t targ, size_t mapsize);

struct mappingdrivers {
  const char *key;
  const char *name;
  open_mapping_type *open;
  close_mapping_type *close;
  alloc_mapping_type *alloc;
  free_mapping_type *free;
  resize_mapping_type *resize;
  alias_mapping_type *alias;
};
char *decode_mapping_cap(int cap);

extern struct mappingdrivers mappingdriver_shm;
extern struct mappingdrivers mappingdriver_mshm;
extern struct mappingdrivers mappingdriver_ashm;
extern struct mappingdrivers mappingdriver_file;

extern int have_mremap_fixed;

void mapping_init(void);
void mapping_close(void);

void init_hardware_ram(void);
int register_hardware_ram(int type, dosaddr_t base, unsigned size);
void register_hardware_ram_virtual(int type, unsigned base, unsigned size,
	dosaddr_t va);
void register_hardware_ram_virtual2(int type, unsigned base, unsigned size,
	void *uaddr, dosaddr_t va);
int unregister_hardware_ram_virtual(dosaddr_t base);
dosaddr_t get_hardware_ram(unsigned addr, uint32_t size);
void *get_hardware_uaddr(unsigned addr);
void list_hardware_ram(void (*print)(const char *, ...));
void *mapping_find_hole(unsigned long start, unsigned long stop,
	unsigned long size);

int mcommit(void *ptr, size_t size);
int muncommit(void *ptr, size_t size);

#endif /* _MAPPING_H_ */
