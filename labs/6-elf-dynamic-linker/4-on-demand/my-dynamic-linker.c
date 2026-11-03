#include "my-dynamic-linker.h"
#include "my-legit-dynamic-linker.h"

// Trust me bro, our global objs exist
extern dl_obj dl_objs[DL_MAX_OBJS];
extern int dl_nobjs;

// Locate essential sections for relocation and dynamic linking.
// Fill in the convenience struct `my_elf32` with the addresses of the sections.
// We can use the .dynamic section to locate .hash, .dynsym, .dynstr, .got.plt, and .rel.dyn sections.
// Refer to 1-10, 2-11, 2-12, and 2-13
// Use the `d_tag` field to identify these sections in the dynamic section.
// The section addresses are stored in the `d_un.d_ptr` field.
void get_dynamic_sections(my_elf32 *e) {
    printk("[MY-DL] Identifying ELF32 dynamic sections...\n");

    char *elf32_base = (char *)e->e_header;
    elf32_header *e_header = (elf32_header *)elf32_base;
    elf32_sheader *e_sheaders = (elf32_sheader *)(elf32_base + e_header->e_shoff);
    
    // Refer to 1-10, 2-11, 2-12, and 2-13
    // Use the `d_tag` field to identify these sections in the dynamic section.
    // The section addresses are stored in the `d_un.d_ptr` field.
    // todo("Locate .hash, .dynsym, .dynstr, .pltgot, and .rel.dyn sections through the .dynamic section");

    for(int i = 0; i < e_header->e_shnum; i++) {
        if(e_sheaders[i].sh_type == SHT_DYNAMIC) {
            // Found dynamic section
            elf32_dynamic *e_dynamics = (elf32_dynamic*) (elf32_base + e_sheaders[i].sh_offset);
            e->e_dynamics = e_dynamics;
            e->n_dynamics = e_sheaders[i].sh_size / sizeof(elf32_dynamic);


            for(int j = 0; j < e->n_dynamics; j++) {
                elf32_dynamic *e_dynamic = &e_dynamics[j];

                switch (e_dynamic->d_tag) {
                    case DT_HASH:
                        e->e_hash = (uint32_t *)(elf32_base + e_dynamic->d_un.d_ptr);
                        break;
                    case DT_SYMTAB:
                        e->e_dynsym = (elf32_sym *)(elf32_base + e_dynamic->d_un.d_ptr);
                        break;
                    case DT_STRTAB:
                        e->e_dynstr = (char *)(elf32_base + e_dynamic->d_un.d_ptr);
                        break;
                    case DT_PLTGOT:
                        e->e_pltgot = (uint32_t *)(elf32_base + e_dynamic->d_un.d_ptr);
                        break;
                    case DT_JMPREL:
                        e->e_reldyn = (elf32_rel *)(elf32_base + e_dynamic->d_un.d_ptr);
                        break;
                }
            }
        }
    }

    if (e->e_hash == NULL || e->e_dynsym == NULL || e->e_dynstr == NULL || e->e_pltgot == NULL || e->e_reldyn == NULL)
        panic("[MY-DL] Couldn't find .hash, .dynsym, .dynstr, .pltgot, or .rel.dyn section\n");
    else
        printk("[MY-DL] Found dynamic sections: .hash: %x, .dynsym: %x, .dynstr: %x, .got.plt: %x, .rel.dyn: %x\n",
            e->e_hash, e->e_dynsym, e->e_dynstr, e->e_pltgot, e->e_reldyn);
}

// Perform load-time relocation of all the symbols in the 
// .got section of this ELF32 file (Read through relocation tables, .rel.plt and .rel.dyn, 
// jump to the symbol table to find the symbol address, and fill in the appropriate entries)
// This is needed because shared libraries are position-independent and thus do not know the
// symbol addresses at runtime.
void load_time_relocation(my_elf32 *e) {
    printk("[MY-DL] Performing load-time relocation of all the symbols in shared library\n");

    char *elf32_base = (char *)e->e_header;
    elf32_header *e_header = (elf32_header *)elf32_base;
    elf32_sheader *e_sheaders = (elf32_sheader *)(elf32_base + e_header->e_shoff);

    for (int i = 0; i < e_header->e_shnum; i++) {

        // Relocation section found
        if (e_sheaders[i].sh_type == SHT_REL) {
            elf32_rel *e_rels = (elf32_rel *)(elf32_base + e_sheaders[i].sh_offset); // Convenience pointer

            for (int j = 0; j * e_sheaders[i].sh_entsize < e_sheaders[i].sh_size; j++) {
                elf32_rel *e_rel = &e_rels[j]; // Convenience pointer

                uint32_t rel_type = e_rel->r_info & 0xff;
                uint32_t symtab_idx = e_rel->r_info >> 8;
                uint32_t entry_addr = (uint32_t)elf32_base + e_rel->r_offset;

                if (rel_type == R_ARM_RELATIVE) { // we should add the base address to the entry
                    *(uint32_t *)entry_addr += (uint32_t)elf32_base;
                } else if (rel_type == R_ARM_GLOB_DAT || rel_type == R_ARM_JUMP_SLOT || rel_type == R_ARM_ABS32) { // we should resolve the symbol in .got section
                    elf32_sym symtab_entry = e->e_dynsym[symtab_idx];

                    uint32_t symbol_addr = symtab_entry.st_value;
                    if (symtab_entry.st_shndx != SHN_UNDEF)
                        // Originally resolved symbol (ex. NOT notmain)
                        symbol_addr += (uint32_t)elf32_base;
                    *(uint32_t *)entry_addr = symbol_addr;

                    // Sanity check
                    // printk("[MY-DL] %d-%d: %s, resolved to %x\n", j, symtab_idx, e->e_dynstr + symtab_entry.st_name, symbol_addr);
                } else {
                    printk("[MY-DL] Unknown relocation type: %d\n", rel_type);
                }
            }
        }
    }
}

unsigned int __aeabi_uidivmod(unsigned int numerator, unsigned int denominator) {
    unsigned int quotient = 0;
    unsigned int remainder = 0;
    
    for (int i = 31; i >= 0; i--) {
        remainder = (remainder << 1) | ((numerator >> i) & 1);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= (1 << i);
        }
    }
    
    return remainder;  // For modulo, return remainder
    // return quotient;  // For division, return quotient
}

// @joshdelg For ELF hashing extension
unsigned long compute_elf_hash(char *symbol_name) {
    unsigned long h = 0, g;

    while (*symbol_name) {
        h = (h << 4) + *symbol_name++;
        if ((g = (h & 0xf0000000))) {
            h ^= g >> 24;
        }
        h &= ~g;
    }

    return h;
}

// Find `symbol_name` in `obj`. Return 0 if not found
uint32_t dl_find_in_obj(dl_obj *obj, char *name) {
    my_elf32* e = &obj->e;
    char *elf32_base = (char *)(obj->e.e_header);
    uint32_t symbol_addr = 0;

    // Fetch number of buckets and chains from .hash section
    uint32_t nbucket = e->e_hash[0];
    uint32_t nchain = e->e_hash[1];

    // printk("[MY-DL] Number of buckets: %d, number of chains: %d\n", nbucket, nchain);

    // Compute hash value and bucket index
    uint32_t hash = compute_elf_hash(name);
    // printk("[MY-DL] Hash: %d\n", hash);

    uint32_t bucket_idx = __aeabi_uidivmod(hash, nbucket);
    // printk("[MY-DL] Bucket index: %d\n", bucket_idx);
    
    uint32_t *bucket = e->e_hash + 2;
    uint32_t *chain = e->e_hash + 2 + nbucket;
    uint32_t chain_idx = bucket[bucket_idx];
    // printk("[MY-DL] Chain index: %d\n", chain_idx);

    elf32_sym *syms = e->e_dynsym;

    // "Follow the chain links"
    elf32_sym *symtab_entry = &syms[chain_idx];
    while(chain_idx != 0) {
        // obj just *imports* this, doesn't acutally define
        if(symtab_entry->st_shndx != SHN_UNDEF) {
            char *sym_name_chain = e->e_dynstr + symtab_entry->st_name;
            // printk("[MY-DL] Symbol name (chain): %s\n", sym_name_chain);

            if (strcmp(sym_name_chain, name) == 0) {
                symbol_addr = (uint32_t) (elf32_base + symtab_entry->st_value);
                printk("[MY-DL] Found symbol (chain): %s at %x\n", name, symbol_addr);
                return symbol_addr;
            }
        }

        // If was incorrect, follow the link
        chain_idx = chain[chain_idx];
        symtab_entry = &syms[chain_idx];
        // printk("[MY-DL] New chain index: %d\n", chain_idx);
    }

    return symbol_addr;
}

// Search for `name` through *all* objs in load order
uint32_t dl_lookup(char* name) {
    for(unsigned i = 0; i < dl_nobjs; i++) {
        uint32_t addr = dl_find_in_obj(&dl_objs[i], name);
        if(addr) {
            printk("[MY-DL] Found symbol %s in obj %s\n", name, dl_objs[i].name);
            return addr;
        }
    }

    panic("[MY-DL] Unable to find symbol %s in any obj\n", name);
}

// Given a symbol name and an elf file, find the address of the symbol in the
// shared library loaded into memory.
// A super fun extension: use the .hash section to find the symbol in O(1) time! -- done!
uint32_t resolve_symbol(my_elf32 *e, char *symbol_name) {
    printk("[MY-DL] Resolving symbol <%s>...\n", symbol_name);
    char *elf32_base = (char *)e->e_header;
    uint32_t symbol_addr = 0;

    // printk("Using %x as the base address\n", elf32_base);


    // @joshdelg Extension (2-22) use .hash table entries

    // Fetch number of buckets and chains from .hash section
    uint32_t nbucket = e->e_hash[0];
    uint32_t nchain = e->e_hash[1];

    printk("[MY-DL] Number of buckets: %d, number of chains: %d\n", nbucket, nchain);

    // Compute hash value and bucket index
    uint32_t hash = compute_elf_hash(symbol_name);
    printk("[MY-DL] Hash: %d\n", hash);
    uint32_t bucket_idx = __aeabi_uidivmod(hash, nbucket);
    printk("[MY-DL] Bucket index: %d\n", bucket_idx);
    uint32_t *bucket = e->e_hash + 2;
    uint32_t *chain = e->e_hash + 2 + nbucket;
    uint32_t chain_idx = bucket[bucket_idx];
    // elf32_sym *symtab_entry = (elf32_sym *) ((uint32_t)(e->e_dynsym) + chain_idx * sizeof(elf32_sym));
    printk("[MY-DL] Chain index: %d\n", chain_idx);

    // Iterate through chain and print indices
    elf32_sym *syms = e->e_dynsym;
    // for(int i = 0; i < nchain; i++) {
    //     printk("[MY-DL] Chain index %d: dynsym index %d -- ", i, chain[i]);
    //     elf32_sym *symtab_entry = &syms[chain[i]];
    //     printk("[MY-DL] Symbol name: %x, %s\n", symtab_entry->st_name, e->e_dynstr + symtab_entry->st_name);
    //     // printk("[MY-DL] Symbol name: %x, %s\n", e->e_dynstr + symtab_entry->st_name, e->e_dynstr + symtab_entry->st_name);
    // }

    // // "Follow the chain links"
    elf32_sym *symtab_entry = &syms[chain_idx];
    while(chain_idx != 0) {
        char *sym_name_chain = e->e_dynstr + symtab_entry->st_name;
        printk("[MY-DL] Symbol name (chain): %s\n", sym_name_chain);

        if (strcmp(sym_name_chain, symbol_name) == 0) {
            symbol_addr = (uint32_t) (elf32_base + symtab_entry->st_value);
            printk("[MY-DL] Found symbol (chain): %s at %x\n", symbol_name, symbol_addr);
            return symbol_addr;
        }
        // If was incorrect, follow the link
        chain_idx = chain[chain_idx];
        symtab_entry = &syms[chain_idx];
        printk("[MY-DL] New chain index: %d\n", chain_idx);
    }


    printk("[MY-DL] Couldn't find symbol: %s, trying brute iteration\n", symbol_name);
    
    // Refer to 1-17 for the symbol entry format and 1-16 for the string table format
    // Go through every entry in .dynsym section, retrieve its index in the .dynstr section, 
    // get the symbol name, compare it with symbol_name, and use st_value to retrieve its address
    // todo("Find the symbol using the .dynsym and .dynstr sections and fill in the symbol_addr");
    
    // Find number of dynsym and dynstr entries by
    // matching address to section header table entries
    elf32_header *e_header = (elf32_header *)elf32_base;
    elf32_sheader *e_sheaders = (elf32_sheader *)(elf32_base + e_header->e_shoff);
    
    uint32_t n_dynsym = 0;
    for(int i = 0; i < e_header->e_shnum; i++) {
        if((uint32_t) (e_sheaders[i].sh_offset + elf32_base) == (uint32_t) e->e_dynsym) {
            n_dynsym = e_sheaders[i].sh_size / sizeof(elf32_sym);
        }
    }

    elf32_sym *e_syms = e->e_dynsym;
    for(int i = 0; i < n_dynsym; i++) {
        // output("Symbol table entry %d: gives index %d into string table\n", i, e->e_dynstr + e_syms[i].st_name);
        char *sym_name = e->e_dynstr + e_syms[i].st_name;
        // output("Considering symbol: %s (index %d)\n", sym_name, i);
        if(strcmp(sym_name, symbol_name) == 0) {
            printk("[MY-DL] Found symbol: %s at %x (dynsym index %d)\n", symbol_name, e_syms[i].st_value, i);
            symbol_addr = (uint32_t) (elf32_base + e_syms[i].st_value);
            break;
        }
    }

    if (symbol_addr == 0)
        panic("[MY-DL] Couldn't find symbol: %s\n", symbol_name);
    else
        printk("[MY-DL] Found symbol: %s at %x\n", symbol_name, symbol_addr);

    return symbol_addr;
}
