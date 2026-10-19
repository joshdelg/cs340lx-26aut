#include "rpi.h"

#define SPI_CLK_DIV 256

enum {
    SPI0_BASE = 0x20204000,
    SPI0_CS   = SPI0_BASE + 0x00,
    SPI0_FIFO = SPI0_BASE + 0x04,
    SPI0_CLK  = SPI0_BASE + 0x08,
    SPI0_DLEN = SPI0_BASE + 0x0C,

    CS_CLEAR_TX = 1 << 4,
    CS_CLEAR_RX = 1 << 5,
    CS_TA       = 1 << 7,
    CS_DMAEN    = 1 << 8,
    CS_DONE     = 1 << 16,
    CS_RXD      = 1 << 17,
    CS_TXD      = 1 << 18,
};

// Tianle Pin Config
enum {
    OE    = 25,
    ADD_A = 22,
    ADD_B = 23,
    CLK   = 11,
    SCLK  = 24,
    DATA  = 10,
};

static void spi_init_p10(unsigned div) {
    gpio_set_function(DATA, GPIO_FUNC_ALT0);   // 10 = MOSI
    gpio_set_function(CLK,  GPIO_FUNC_ALT0);   // 11 = SCLK
    
    dev_barrier();
    
    PUT32(SPI0_CS, CS_CLEAR_TX | CS_CLEAR_RX); // mode 0, TA off
    PUT32(SPI0_CLK, div);
    
    dev_barrier();
}

static void spi_init_p10_dma(unsigned div, unsigned nbytes) {
    spi_init_p10(div);

    PUT32(SPI0_DLEN, nbytes);

    // Enable DMA, TA off: the DMA chain starts each frame itself
    PUT32(SPI0_CS, CS_CLEAR_TX | CS_CLEAR_RX | CS_DMAEN);

    dev_barrier();
}

// CPU-polled sanity test: replaces the shift_byte loop.
static void spi_send_poll(const uint8_t *buf, unsigned n) {
    PUT32(SPI0_CS, CS_CLEAR_TX | CS_CLEAR_RX | CS_TA);
    for(unsigned i = 0; i < n; i++) {
        while(!(GET32(SPI0_CS) & CS_TXD)) ;
        PUT32(SPI0_FIFO, buf[i]);
        while(GET32(SPI0_CS) & CS_RXD)       // drain RX, same reason as the DMA drain
            (void)GET32(SPI0_FIFO);
    }
    while(!(GET32(SPI0_CS) & CS_DONE))
        while(GET32(SPI0_CS) & CS_RXD) (void)GET32(SPI0_FIFO);
    PUT32(SPI0_CS, 0);                         // TA off
}
