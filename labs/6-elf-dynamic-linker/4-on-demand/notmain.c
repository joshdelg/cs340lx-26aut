#include "rpi.h"
#include "my-legit-dynamic-linker.h"
#include "my-elf-loader.h"

// Change this to the filename of the dynamically linked ELF file you want to load
// static char *exec_filename = "0-DYN.ELF";
// static char *exec_filename = "1-DYN.ELF";
// static char *exec_filename = "2-DYN.ELF";
static char *exec_filename = "3-DYN.ELF";
static char *exec_base = (char *)0x0;
my_elf32 *exec_e;

static char *libpi_filename = "LIBPI.SO";
my_elf32 *libpi_e;

// @joshdelg Extension B: Factor per-object steps into general library loader
static dl_obj dl_objs[DL_MAX_OBJS];
static int dl_nobjs = 0; // dl_objs[0] = executable
static char *dl_next_base = (char *)DL_LIB_REGION_START; // exec is always at 0

static uint32_t align_up(uint32_t x, uint32_t a) {
    return (x + a - 1) & ~(a - 1);
}

// Convert to uppercase and ensure length
static void dl_fat_name(char *dst, const char *src, unsigned n) {
    unsigned i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        char c = src[i];
        dst[i] = (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
    }
    dst[i] = 0;
    
    if (src[i])
        panic("[MY-DL] library name too long for 8.3: %s\n", src);
}

dl_obj *load_object(char* name, char* base) {
    assert(dl_nobjs < DL_MAX_OBJS);
    dl_obj *obj = &dl_objs[dl_nobjs++];

    uint32_t size = load_elf(name, base);

    elf32_header *object_header = (elf32_header *)base;
    verify_elf(object_header);

    bss_zero_init(object_header);

    obj->e.e_header = object_header;
    safe_strcpy(obj->name, name, sizeof(obj->name));
    obj->base = base;
    obj->e.e_sheaders = (elf32_sheader *)(base + object_header->e_shoff);
    obj->e.e_pheaders = (elf32_pheader *)(base + object_header->e_phoff);
    get_dynamic_sections(&obj->e);

    for (int i = 0; i < object_header->e_phnum; i++) {
        elf32_pheader *ph = &obj->e.e_pheaders[i];
        // Check if any sections would extend past the end of the file, 
        if (ph->p_type == PT_LOAD && ph->p_vaddr + ph->p_memsz > size)
            size = ph->p_vaddr + ph->p_memsz;
    }
    obj->size = size;

    return obj;
}

// Load the object and position it in memory
dl_obj *load_library(char *name) {
    dl_obj *obj = load_object(name, dl_next_base);
    dl_next_base = (char *)align_up((uint32_t)obj->base + obj->size, DL_LIB_ALIGN);
    assert((uint32_t)dl_next_base <= DL_LIB_REGION_END);
    return obj;
}

// Scan dl_objs to find the one with this name
static dl_obj *dl_find_loaded(const char *name) {
    for (int i = 0; i < dl_nobjs; i++) {
        if (strcmp(dl_objs[i].name, name) == 0) return &dl_objs[i];
    }
    
    return NULL;
}

// Load every DT_NEEDED library of `obj` that isn't already loaded
static void dl_load_needed(dl_obj *obj) {
    my_elf32 *e = &obj->e;

    for (int i = 0; i < e->n_dynamics; i++) {
        elf32_dynamic *d = &e->e_dynamics[i];
        if (d->d_tag == DT_NULL)
            break;

        if (d->d_tag == DT_NEEDED) {
            char name[13];
            dl_fat_name(name, e->e_dynstr + d->d_un.d_val, sizeof name);
            if (dl_find_loaded(name)) {
                printk("[MY-DL] %s needs %s (already loaded)\n", obj->name, name);
                continue;
            }
            printk("[MY-DL] %s needs %s, loading\n", obj->name, name);
            load_library(name);
        }
    }
}

void notmain() {

    // Extension: all of the below can be done on the pi-side bootloader

    // Load the exe first, manually at 0x0
    dl_obj *exe = load_object(exec_filename, exec_base);
    exec_e = &exe->e;

    // Load all libraries breath-first (newly needed libs added to back, which will then be explored)
    for (int i = 0; i < dl_nobjs; i++) // dl_nobjs will get incremented by dl_load_needed
        dl_load_needed(&dl_objs[i]);

    for (int i = 0; i < dl_nobjs; i++)
        printk("[MY-DL] obj %d: %s @ %x, size %x\n", i, dl_objs[i].name, dl_objs[i].base, dl_objs[i].size);

    // TODO: Stop assuming this is the only lib loaded
    dl_obj *libpi = dl_find_loaded("LIBPI.SO");
    libpi_e = &libpi->e;

    // (Already done for you) Resolve all undefined symbols in the .dynsym section of the 
    // libpi.so. This is needed because some symbols in libpi.so exist in our main executable (e.g., `notmain`),
    // so it must be resolved at load-time.
    load_time_resolve(exec_e, libpi_e);

    // Perform load-time relocation for position-independent code (part 2)
    load_time_relocation(libpi_e);

    // The only step needed for true runtime dynamic linking: fill in .got.plt[2] with the 
    //  address of the dynamic linker entry function.
    // This is the address that the program will jump to when it reaches an unresolved symbol.
    exec_e->e_pltgot[2] = (uint32_t)dynamic_linker_entry_asm;

    // Jump to the entry point of the ELF file. The dynamic linking will automatically happen!
    jump_to_elf_entry(exe->e.e_header);
}
