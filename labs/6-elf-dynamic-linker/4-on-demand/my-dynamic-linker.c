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

    // if (e->e_hash == NULL || e->e_dynsym == NULL || e->e_dynstr == NULL || e->e_pltgot == NULL || e->e_reldyn == NULL)
    if (e->e_hash == NULL || e->e_dynsym == NULL || e->e_dynstr == NULL)
        panic("[MY-DL] Couldn't find .hash, .dynsym, .dynstr\n");
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

                    // @joshdelg Before, we ran `resolve_imports` first which for all UNDEF symbols, 
                    // would resolve and write the absolute address to `st_value`
                    // Thus, here, UNDEF were ready to go, while !UNDEF symbols needed the base added (st_value already had addr of local definition)
                    // -> This amounted to assuming we would just use the defined version, instead of checking if it could be overridden

                    // Now, we want to support overridding symbols. If a symbol can be overridden, we
                    // want to search for it in dl_objs order (using dl_lookup). An added benefit is
                    // now that we're looking up the symbols during relocation, we no longer need the
                    // earlier `resolve_imports` stage.

                    // What address for symbol? If can't be overridden, use the existing local def addr, otherwise lookup
                    uint8_t local_symbol = (symtab_entry.st_info >> 4) == STB_LOCAL;
                    char* name = e->e_dynstr + symtab_entry.st_name;
                    uint32_t symbol_addr = local_symbol ? (uint32_t)elf32_base + symtab_entry.st_value : dl_lookup(name);
                    
                    if(rel_type == R_ARM_ABS32) { // Keep the addend already at the entry addr
                        *(uint32_t*)entry_addr += symbol_addr;
                    } else { // Just replace
                        *(uint32_t *)entry_addr = symbol_addr;
                    }

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
                // printk("[MY-DL] Found symbol (chain): %s at %x\n", name, symbol_addr);
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