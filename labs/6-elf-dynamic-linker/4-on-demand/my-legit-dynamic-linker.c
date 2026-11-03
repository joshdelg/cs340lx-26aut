#include "rpi.h"
#include "my-legit-dynamic-linker.h"

// Called by my_dl_entry_asm
// gotplt: the address of the third entry of .got.plt
// gotplt_entry: the address of the unresolved symbol's .got.plt entry
// Performs dynamic linking and resolves the symbol, saves its address in gotplt_entry
// Returns the address of the resolved symbol
uint32_t dynamic_linker_entry_c(uint32_t *gotplt, uint32_t *gotplt_entry) {

    // Useful for debugging: read what is in stack
    // volatile uint32_t *sp;
    // asm volatile("mov %0, sp" : "=r"(sp));

    // Useful for debugging: print out the values of gotplt and gotplt_entry
    printk("[MY-DL] gotplt: %x, gotplt_entry: %x\n", gotplt, gotplt_entry);
    printk("[MY-DL] *gotplt: %x, *gotplt_entry: %x\n", *gotplt, *gotplt_entry);

    // The first entry of .got.plt is the address to .dynamic section
    // elf32_dynamic *e_dynamics = (elf32_dynamic *)(*(gotplt - 2));
    // ...But we can just use our utility struct my_elf32

    // The recipe:
    //   1. Find the unresolved symbol index in `.rel.dyn` section. Hint: since we already know `&GOT[2]` and `&GOT[i+3]`, we can subtract these addresses to find `i`.
    //   2. Retrieve the `.dynsym` index from the `.rel.dyn` entry. You can do this by taking the relocation entry's INFO field and bitwise shifting it to the right by 8 (the first 8 bits hold other data).
    //   3. Retrieve symbol's string representation from the `.dynstr` section.
    //   4. Call `resolve_symbol` function (from part 2) to retrieve the symbol address.
    //   5. Fill in `GOT[i+3]` with symbol address
    //   6. Return the symbol address

    // @joshdelg: GOT[1] now has ptr to dl_obj (which is not necessarily the exec now!)
    dl_obj *obj = (dl_obj*)gotplt[-1];
    my_elf32 *e = &obj->e;
    
    // Calculate the index of the symbol in the .rel.dyn section
    uint32_t i = gotplt_entry - gotplt - 1;

    uint32_t dynsym_index = (uint32_t) (e->e_reldyn[i].r_info >> 8);
    char *symbol_name = e->e_dynstr + e->e_dynsym[dynsym_index].st_name;

    printk("[MY-DL] Dynamic linker: Unresolved symbol encountered: <%s>. Dynamic linker invoked.\n", symbol_name);

    // Try to resolve the symbol and fill in the got table entry (now use dl_lookup)
    uint32_t symbol_addr = dl_lookup(symbol_name);
    *gotplt_entry = symbol_addr;
    printk("[MY-DL] Dynamic linker: Resolved symbol %s from obj %s to %x\n", symbol_name, obj->name, symbol_addr);

    return symbol_addr; // the rest should be handled by asm
}
