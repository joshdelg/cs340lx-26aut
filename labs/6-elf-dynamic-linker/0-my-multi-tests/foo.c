// libfoo.so: needs libbar.so and libpi.so
#include "rpi.h"

void bar_hello(void);
int bar_twice(int x);

void foo_hello(void) {
    printk("hello from libfoo\n");
    bar_hello();
}

int foo_add(int a, int b) {
    return bar_twice(a) + b;
}

// Also defined in libbar, lookup order should decide what to use
const char *whoami(void) {
    return "libfoo";
}
