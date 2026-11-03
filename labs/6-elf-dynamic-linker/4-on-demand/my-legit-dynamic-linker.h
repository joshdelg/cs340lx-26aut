#ifndef __MY_LEGIT_DYNAMIC_LINKER_H__
#define __MY_LEGIT_DYNAMIC_LINKER_H__

#include "rpi.h"
#include "my-dynamic-linker.h"

// - This is the entry point for the dynamic linker
// - The address of this label is stored in the third entry of .got section at load-time
// - During runtime, when a program reaches an unresolved symbol, it will eventually 
//   jump to this address (after going through .plt and .got.plt entries)
// - Registers and stack will hold:
//   - r0~r3: arguments to the unresolved symbol
//   - r12: address to the .got.plt entry that should be filled with 
//          the address of the resolved symbol
//   - lr: holds the address to the third entry of the .got.plt section, which holds 
//         the address of the dynamic linker entry function. 
//   - stack: holds one 4B word that is the return address. Should be stored in lr 
//            before jumping to the resolved symbol
void dynamic_linker_entry_asm(void);

// Called by my_dl_entry_asm
// gotplt: address of the third entry of .got.plt
// gotplt_entry: address of the unresolved symbol's .got.plt entry
// Performs dynamic linking and resolves the symbol, saves its address in gotplt_entry
// Returns the address to the resolved symbol
uint32_t dynamic_linker_entry_c(uint32_t *gotplt, uint32_t *gotplt_entry);

// @joshdelg Extension B
typedef struct {
    my_elf32 e;
    char name[13];
    char* base;
    uint32_t size;
} dl_obj;

// Address of `name` if `obj` defines it (skips symbols it only imports), else 0
uint32_t dl_find_in_obj(dl_obj *obj, char *name);

#define DL_MAX_OBJS 64
#define DL_LIB_REGION_START 0x10000000
#define DL_LIB_REGION_END   0x20000000   // peripherals start here (see the README memory map)
#define DL_LIB_ALIGN        0x100000     // 1MB: matches a VM section, in case you do extension C

void dl_resolve_imports(dl_obj *obj);

#endif
