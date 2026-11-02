#include "rpi.h"
#include "gpio.h"
#include "rpi-interrupts.h"
#include "vector-base.h"

#include "dma-impl.h"
#include "pcm.h"

#define SAMPLE_RATE 48000
#define NBUF        4
#define BUF_FRAMES  1024

#define DMA_CH      4
#define DMA_IRQ     (16 + DMA_CH) // DMA0 = IRQ 16

enum {
    DMA_CS_ACTIVE = 1 << 0,
    DMA_CS_INT    = 1 << 2
};

// interleaved L, R, L, R, ...
static uint32_t bufs[NBUF][BUF_FRAMES * 2] __attribute__((aligned(32)));
// cbs[i] copies bufs[i] into the FIFO, then runs cbs[i + 1]
static cb_t cbs[NBUF];
// Next CPU write
static unsigned head = 1;
// Keep sample count here so phase doesn't reset
static uint32_t phase_n = 0;

static dma_ch_t *dma;
// Playing DMA buffer
static volatile unsigned playing = 0;

static uint32_t render_us = 0;

// 440Hz square wave
static void render_square(uint32_t *buf) {
    for(uint32_t i = 0; i < BUF_FRAMES; i++) {
        int32_t sample = ((phase_n * 440) % SAMPLE_RATE < SAMPLE_RATE / 2) ? (1 << 28) : -(1 << 28);
        buf[2 * i] = sample;     // left
        buf[2 * i + 1] = sample; // right
        phase_n = (phase_n + 1) % SAMPLE_RATE;
    }
}

// SAD: Linear scan to find DMA buffer playing
static unsigned dma_playing(dma_ch_t *dma) {
    bus_t cb_addr = dma->CB_ADDR;
    for(unsigned i = 0; i < NBUF; i++)
        if(bus_eq(cb_addr, bus(&cbs[i])))
            return i;
    panic("CB_ADDR %x isn't one of ours\n", cb_addr._0);
}

// Init all the control blocks and connect in a ring
static void ring_init(void) {
    for(unsigned i = 0; i < NBUF; i++) {
        cbs[i] = cb_mk_with(bus((void *)PCM_FIFO_A), bus(bufs[i]), sizeof bufs[i],
                            INTEN | SRC_INC | DST_DREQ | WAIT_RESP | (DREQ_PCM_TX << PERMAP_OFFSET));
        cbs[i].NEXT_CB = bus(&cbs[(i + 1) % NBUF]);
    }
}

// Write audio data into the next buffer, and silence into the one after (in case DMA goes ahead)
static void ring_write_next(void) {
    // Wait until neither this buffer nor the one after are playing to write them
    while(head == playing || (head + 1) % NBUF == playing) {}

    uint32_t start = timer_get_usec();
    render_square(bufs[head]);
    render_us = timer_get_usec() - start;
    head = (head + 1) % NBUF;
    __builtin_memset(bufs[head], 0, sizeof bufs[head]);
}

// Int each time a control block finishes
void int_vector(uint32_t pc) {
    dev_barrier();

    if(!(GET32(IRQ_pending_1) & (1 << DMA_IRQ)))
        panic("unexpected interrupt\n");

    // Clear INT
    dma->CS = DMA_CS_INT | DMA_CS_ACTIVE;
    playing = dma_playing(dma);
    dev_barrier();
}

void notmain() {
    for(unsigned i = 0; i < sizeof(PCM_PINS) / sizeof(unsigned); i++) {
        gpio_set_function(PCM_PINS[i], GPIO_FUNC_ALT0);
    }

    ring_init();

    extern uint32_t default_vec_ints[];
    dev_barrier();
    PUT32(IRQ_Disable_1, 0xffffffff);
    PUT32(IRQ_Disable_2, 0xffffffff);
    dev_barrier();
    vector_base_set(default_vec_ints);

    dma = dma_init(DMA_CH);

    pcm_start();

    // Start the DMA ring, then TX
    dma_initiate(dma, &cbs[0]);
    pcm_tx_on();

    PUT32(IRQ_Enable_1, 1 << DMA_IRQ);
    enable_interrupts();

    for(unsigned nbufs = 1; ; nbufs++) {
        ring_write_next();

        if(nbufs % 50 == 0) {
            printk("render %d us\n", render_us);
        }
    }
}
