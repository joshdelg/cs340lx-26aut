#pragma once

#include "dma-impl.h"
#include "pcm.h"

enum { NOTE_OFF = 5, NNOTES = 6, NOTE_DMA_CH = 5 };

// Scale degree freqs
static const unsigned note_period[NNOTES] = { 183, 163, 146, 137, 122, 128 };

static uint32_t note_buf[NNOTES][183 * 2] __attribute__((aligned(32)));
static cb_t note_cb[NNOTES];

enum { NOTE_AMPLITUDE = 0x7fffffff / 8 };

// Sample i of a triangle wave with period p samples
static int32_t triangle_sample(unsigned i, unsigned p) {
    int32_t half = p / 2;
    int32_t pos = i < p / 2 ? i : p - i;   // distance up the ramp: 0..half

    int32_t step = NOTE_AMPLITUDE / half;
    return (2 * pos - half) * step;
}

// Populate samples
static void notes_init(void) {
    for(unsigned i = 0; i < sizeof PCM_PINS / sizeof PCM_PINS[0]; i++)
        gpio_set_function(PCM_PINS[i], GPIO_FUNC_ALT0);

    for(unsigned n = 0; n < NNOTES; n++) {
        unsigned p = note_period[n];
        for(unsigned i = 0; i < p; i++) {
            int32_t s = (n == NOTE_OFF) ? 0 : triangle_sample(i, p);

            note_buf[n][2 * i]     = s;
            note_buf[n][2 * i + 1] = s;
        }


        note_cb[n] = cb_mk_with(bus((void *)PCM_FIFO_A), bus(note_buf[n]), p * 2 * 4,
                                SRC_INC | DST_DREQ | WAIT_RESP | (DREQ_PCM_TX << PERMAP_OFFSET));
        
        note_cb[n].NEXT_CB = bus(&note_cb[n]);
    }

    dma_ch_t *dma = dma_init(NOTE_DMA_CH);
    pcm_start();
    
    dma_initiate(dma, &note_cb[NOTE_OFF]);
    pcm_tx_on();
}

static void note_play(unsigned n) {
    assert(n < NNOTES);
    for(unsigned i = 0; i < NNOTES; i++)
        note_cb[i].NEXT_CB = bus(&note_cb[n]);
}
