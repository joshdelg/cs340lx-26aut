// Use PL1101 interrupts on RTS pin to blink LED at a frequency
#include "rpi.h"
#include "pl011.h"
#include "bt.h"
#include "rpi-interrupts.h"
#include "copro.h"

enum { LED = 21, HALF_PERIOD_US = 500000, TOGGLES = 20 };
enum {
    UART_IMSC = 0x20201038, // interrupt mask
    UART_ICR  = 0x20201044, // interrupt clear
    CTS_INT   = 1 << 1,     // CTS changed
    UART_IRQ  = 1 << (57 - 32), // IRQ 57, IRQ_Enable_2
};

static volatile unsigned toggles;

void interrupt_vector(unsigned pc) {
    dev_barrier();
    PUT32(UART_ICR, CTS_INT);
    unsigned on = !(GET32(UART_FR) & FR_CTS); // on while RTS is deasserted
    dev_barrier();
    gpio_write(LED, on);
    toggles++;
    dev_barrier();
}

void notmain(void) {
    gpio_set_output(LED);
    copro_load();

    interrupt_init();
    PUT32(UART_ICR, CTS_INT);
    PUT32(UART_IMSC, CTS_INT);
    PUT32(IRQ_Enable_2, UART_IRQ);

    copro_start(UART_MODE, RTS, HALF_PERIOD_US, TOGGLES);
    enable_interrupts();
    while (copro_running())
        ; // Busy wait to prove the BT is doing this!
    disable_interrupts();
    copro_finish();

    if (toggles < TOGGLES)
        panic("only %d toggles", toggles);
    trace("the radio blinked the LED [%d interrupts]\n", toggles);
}
