/* Is bit 5 of the radio's UART mode word its RTS, which the Pi sees live as CTS?
 * Result: yes. The Pi sees all 6 flips while the radio runs, 50 ms apart to
 * within a few us. */
#include "rpi.h"
#include "pl011.h"
#include "bt.h"
#include "copro.h"

enum { FLIPS = 6, GAP_US = 50000 };

void notmain(void) {
    copro_load();
    copro_start(UART_MODE, RTS, GAP_US, FLIPS);

    unsigned start = timer_get_usec(), cts = GET32(UART_FR) & FR_CTS;
    unsigned flips = 0, last = 0, worst = 0;
    while (copro_running()) {
        if ((GET32(UART_FR) & FR_CTS) == cts)
            continue;
        cts ^= FR_CTS;

        unsigned now = timer_get_usec() - start;
        if (now < GAP_US / 2)
            continue;                           /* the ROM's blip at launch */
        if (flips++) {
            unsigned d = now - last, off = d > GAP_US ? d - GAP_US : GAP_US - d;
            if (off > worst)
                worst = off;
        }
        last = now;
    }
    copro_finish();

    if (flips != FLIPS || worst > 100)
        panic("saw %d flips, worst %d us off", flips, worst);
    trace("the Pi saw all %d RTS flips live [worst %d us off]\n", FLIPS, worst);
}
