#include "rpi.h"
#include "gpio.h"
#include "gpio-raw.h"
#include "cycle-count.h"

enum { PIN = 23 };

static uint32_t time_gpio(uint32_t pin, int start_lev, void (*set_pull)(unsigned), uint32_t invert) {
    // Drive the pin
    gpio_set_output(pin);
    gpio_write(pin, start_lev);
    
    // Set pull dir (or none)
    set_pull(pin);

    // Count cycles while pin is still at that level
    uint32_t s = cycle_cnt_read();
    gpio_set_input_raw(pin);
    while(gpio_read_raw(pin) == (invert ? !start_lev : start_lev)) {}
    
    return cycle_cnt_read() - s;
}

void notmain() {

    // Time measurement overhead
    uint32_t offset_acc = 0;
    for(int i = 0; i < 10; i++) {
        uint32_t offset_cyc = time_gpio(PIN, 1, gpio_set_pullup, 1);
        offset_acc += offset_cyc;
    }
    uint32_t offset_avg = offset_acc / 10;
    output("constant offset (avg) = %d cycles\n", offset_avg);

    // Test pullup
    uint32_t cycles = time_gpio(PIN, 0, gpio_set_pullup, 0);

    output("Pullup took %d cycles (%d after subtracting offset)\n",
        cycles, cycles - offset_avg);

    // Test pulldown
    cycles = time_gpio(PIN, 1, gpio_set_pulldown, 0);

    output("Pulldown took %d cycles (%d after subtracting offset)\n",
        cycles, cycles - offset_avg);

    // Test no pull
    cycles = time_gpio(PIN, 1, gpio_set_pud_off, 0);

    output("No pull took %d cycles (%d after subtracting offset)\n",
        cycles, cycles - offset_avg);
}