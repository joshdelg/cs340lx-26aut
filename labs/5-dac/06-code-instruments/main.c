#include "rpi.h"
#include "gpio.h"
#include "rpi-interrupts.h"
#include "vector-base.h"

#include "dma-impl.h"
#include "pcm.h"
#include "instrument.h"

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

static dma_ch_t *dma;
// Playing DMA buffer
static volatile unsigned playing = 0;

// Demo song: each instrument plays a C4 E4 G4 C5 arpeggio (a note every 250ms, held 200ms),
// then a C4+E4+G4 chord held for 1s, then 1s of rest to hear it ring out
#define MS(ms)        ((ms) * 48)
#define PHRASE_LEN    MS(3000)

typedef struct {
    uint32_t at; // sample offset from song start
    unsigned on; // 1 = note on, 0 = note off
    unsigned note;
    unsigned inst;
} event_t;

#define EVENTS_PER_PHRASE 14
static event_t events[NINSTRUMENTS * EVENTS_PER_PHRASE];
static unsigned nevents = 0;
static const uint32_t song_len = NINSTRUMENTS * PHRASE_LEN;

// Song position in samples, and the next event to apply
static uint32_t song_clock = 0;
static unsigned next_ev = 0;

static uint32_t render_us = 0;

static void ev(uint32_t at, unsigned on, unsigned note, unsigned inst) {
    events[nevents++] = (event_t){ .at = at, .on = on, .note = note, .inst = inst };
}

static void song_init(void) {
    static const unsigned arp[] = { 60, 64, 67, 72 };
    static const unsigned chord[] = { 60, 64, 67 };
    for(unsigned i = 0; i < NINSTRUMENTS; i++) {
        uint32_t t = i * PHRASE_LEN;
        for(unsigned n = 0; n < 4; n++) {
            ev(t + MS(250 * n), 1, arp[n], i);
            ev(t + MS(250 * n + 200), 0, arp[n], i);
        }
        for(unsigned n = 0; n < 3; n++)
            ev(t + MS(1000), 1, chord[n], i);
        for(unsigned n = 0; n < 3; n++)
            ev(t + MS(2000), 0, chord[n], i);
    }
}

// Render one buffer, splitting it at each event so notes start on their exact sample
static void render_buffer(uint32_t *buf) {
    unsigned pos = 0;
    while(pos < BUF_FRAMES) {
        for(; next_ev < nevents && events[next_ev].at <= song_clock; next_ev++) {
            event_t *e = &events[next_ev];
            if(e->on)
                note_on(e->inst, &INSTRUMENTS[e->inst], e->note, 0.8f);
            else
                note_off(e->inst, e->note);
        }

        // Render up to the next event, the end of the song, or the end of the buffer
        uint32_t until = next_ev < nevents ? events[next_ev].at : song_len;
        unsigned n = BUF_FRAMES - pos;
        if(until - song_clock < n)
            n = until - song_clock;

        voices_render(buf + 2 * pos, n);
        pos += n;
        song_clock += n;

        if(song_clock >= song_len) {
            song_clock = 0;
            next_ev = 0;
        }
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
    render_buffer(bufs[head]);
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

    instruments_init();
    song_init();
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
            printk("%s: render %d us\n", INSTRUMENTS[song_clock / PHRASE_LEN].name, render_us);
        }
    }
}
