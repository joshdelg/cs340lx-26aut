#include "rpi.h"
#include "gpio-raw.h"

static int wrong = 0;

void notmain() {

    // caches_enable();

    gpio_set_pulldown(12);

    gpio_set_output(12);
    gpio_set_on_raw(12);

    gpio_set_input(12);

    while(gpio_read_raw(12) != 0) {
        wrong++;
    }

    output("Wrong: %d\n", wrong);
}