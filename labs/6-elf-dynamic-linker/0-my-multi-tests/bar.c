// libbar.so: needed by libfoo.so, but NOT by multi.elf directly
#include "rpi.h"

const char *whoami(void);

void bar_hello(void) {
    printk("hello from libbar (whoami says: %s)\n", whoami());
}

int bar_twice(int x) {
    return 2 * x;
}

// Also defined in libfoo, lookup order should decide what to use
const char *whoami(void) {
    return "libbar";
}
