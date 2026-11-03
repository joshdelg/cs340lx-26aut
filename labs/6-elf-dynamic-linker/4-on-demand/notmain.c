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

void notmain() {

    // Extension: all of the below can be done on the pi-side bootloader

    // // Load main executable and libpi.so into memory (part 1)
    // load_elf(exec_filename, exec_base);
    // load_elf(libpi_filename, libpi_base);

    // // Verify the ELF headers (part 1)
    // elf32_header *exec_e_header = (elf32_header *)exec_base;
    // elf32_header *libpi_e_header = (elf32_header *)libpi_base;
    // verify_elf(exec_e_header);
    // verify_elf(libpi_e_header);

    // // Zero-initialize the .bss sections (part 1)
    // bss_zero_init(exec_e_header);
    // bss_zero_init(libpi_e_header);

    // // Locate dynamic sections (part 2)
    // exec_e.e_header = exec_e_header;
    // exec_e.e_sheaders = (elf32_sheader *)(exec_base + exec_e_header->e_shoff);
    // exec_e.e_pheaders = (elf32_pheader *)(exec_base + exec_e_header->e_phoff);
    // libpi_e.e_header = libpi_e_header;
    // libpi_e.e_sheaders = (elf32_sheader *)(libpi_base + libpi_e_header->e_shoff);
    // libpi_e.e_pheaders = (elf32_pheader *)(libpi_base + libpi_e_header->e_phoff);
    // get_dynamic_sections(&exec_e);
    // get_dynamic_sections(&libpi_e);

    dl_obj *exe = load_object(exec_filename, exec_base);
    exec_e = &exe->e;
    dl_obj *libpi = load_library(libpi_filename);
    libpi_e = &libpi->e;

    // (Already done for you) Resolve all undefined symbols in the .dynsym section of the 
    // libpi.so. This is needed because some symbols in libpi.so exist in our main executable (e.g., `notmain`),
    // so it must be resolved at load-time.
    load_time_resolve(exec_e, libpi_e);
    // load_time_resolve(&exe->e, &libpi->e);

    // Perform load-time relocation for position-independent code (part 2)
    load_time_relocation(libpi_e);
    // load_time_relocation(&libpi->e);

    // The only step needed for true runtime dynamic linking: fill in .got.plt[2] with the 
    //  address of the dynamic linker entry function.
    // This is the address that the program will jump to when it reaches an unresolved symbol.
    exec_e->e_pltgot[2] = (uint32_t)dynamic_linker_entry_asm;
    // exe->e.e_pltgot[2] = (uint32_t)dynamic_linker_entry_asm;

    for (int i = 0; i < dl_nobjs; i++)
        printk("[MY-DL] obj %d: %s @ %x, size %x\n", i, dl_objs[i].name, dl_objs[i].base, dl_objs[i].size);

    // Jump to the entry point of the ELF file. The dynamic linking will automatically happen!
    // jump_to_elf_entry(exec_e_header);
    jump_to_elf_entry(exe->e.e_header);
}
