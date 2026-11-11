// flip `mask` in register `reg` n times, `gap` cycles apart, then restore it. The arguments are at RESULT_AREA
#include <stdint.h>
#include "bt.h"

#define REG(a) (*(volatile uint32_t *)(a))
enum { DEMCR = 0xe000edfc, DWT_CTRL = 0xe0001000, DWT_CYCCNT = 0xe0001004 };

void radio_main(void) {
    uint32_t *a = (uint32_t *)RESULT_AREA;
    uint32_t reg = a[0], mask = a[1], gap = a[2], n = a[3], orig = REG(reg);

    REG(DEMCR) |= 1 << 24; // Enable cycle counter
    REG(DWT_CTRL) |= 1;

    uint32_t deadline = REG(DWT_CYCCNT);
    for (uint32_t i = 0; i < n; i++) {
        deadline += gap;
        while ((int32_t)(REG(DWT_CYCCNT) - deadline) < 0)
            ;
        REG(reg) ^= mask;
    }
    REG(reg) = orig;
}
