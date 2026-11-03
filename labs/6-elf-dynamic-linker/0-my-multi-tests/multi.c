// Linked against libfoo.so and libpi.so only. libbar.so must get pulled in
// through libfoo.so's DT_NEEDED.
#include "rpi.h"

void foo_hello(void);
int foo_add(int a, int b);
const char *whoami(void);

void notmain() {
    printk("multi: start\n");

    // printk: exe -> libfoo -> libpi, barhello(): libfoo -> libbar -> libpi
    foo_hello();

    // libfoo calls into libbar, expect 2*2 + 3 = 7
    printk("foo_add(2, 3) = %d\n", foo_add(2, 3));

    // Defined in both libfoo and libbar -- libfoo is loaded first so should see "libfoo"
    printk("exe's whoami: %s\n", whoami());

    clean_reboot();
}
