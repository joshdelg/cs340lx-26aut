#ifndef COPRO_H
#define COPRO_H

enum { M3_HZ = 24000000 }; // Cycle counter
enum { UART_MODE = 0x360420, RTS = 1 << 5 }; // RTS bit

static const uint8_t copro[] = {
#include "copro_image.h"
};

static void copro_load(void) {
    pl011_up(115200, CR_RTS);
    bt_reset(10);
    bt_connect(500 * 1000);
    for (unsigned off = 0; off < sizeof copro; off += 240) {   /* 251 max */
        unsigned n = sizeof copro - off;
        bt_write_ram(LOAD + off, copro + off, n < 240 ? n : 240);
    }
}

// Don't wait for Command Complete -- messages buffered
static void copro_start(unsigned reg, unsigned mask, unsigned gap_us, unsigned n) {
    unsigned args[] = { reg, mask, gap_us * (M3_HZ / 1000000), n }, at = LOAD | 1;
    bt_write_ram(RESULT_AREA, args, sizeof args);

    uint8_t launch[] = { 1, LAUNCH_RAM & 0xff, LAUNCH_RAM >> 8, 4,
                         at, at >> 8, at >> 16, at >> 24 };
                         
    for (unsigned i = 0; i < sizeof launch; i++) {
        while (GET32(UART_FR) & FR_TXFF)
            ;
        PUT32(UART_DR, launch[i]);
    }
}

static int copro_running(void) {
    return GET32(UART_FR) & FR_RXFE;
}

// Now, wait for the Command Complete
static void copro_finish(void) {
    static const uint8_t done[] = { 4, 0x0e, 4, 1, LAUNCH_RAM & 0xff, LAUNCH_RAM >> 8, 0 };
    for (unsigned i = 0; i < sizeof done; i++) {
        while (GET32(UART_FR) & FR_RXFE)
            ;
        unsigned b = GET32(UART_DR);
        if (b != done[i])
            panic("Launch_RAM's Command Complete: byte %d is %x", i, b);
    }
}
#endif
