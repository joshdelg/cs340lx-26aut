#include "rpi.h"
#include "gpio.h"

#include "dma-impl.h"
#include "pcm.h"

#define SAMPLE_RATE 48000
#define NFRAMES     SAMPLE_RATE // 1 second

#define DMA_CH      4

// interleaved L, R, L, R, ...
static uint32_t audio[NFRAMES * 2] __attribute__((aligned(32)));
// Copies all of audio into the FIFO, then stops
static cb_t audio_cb;

// 440Hz square wave
static void render_square(void) {
    for(uint32_t i = 0; i < NFRAMES; i++) {
        int32_t sample = ((i * 440) % SAMPLE_RATE < SAMPLE_RATE / 2) ? (1 << 28) : -(1 << 28);
        audio[2 * i] = sample;     // left
        audio[2 * i + 1] = sample; // right
    }
}

void notmain() {
    for(unsigned i = 0; i < sizeof(PCM_PINS) / sizeof(unsigned); i++) {
        gpio_set_function(PCM_PINS[i], GPIO_FUNC_ALT0);
    }

    render_square();

    audio_cb = cb_mk_with(bus((void *)PCM_FIFO_A), bus(audio), sizeof audio,
                          SRC_INC | DST_DREQ | WAIT_RESP | (DREQ_PCM_TX << PERMAP_OFFSET));

    dma_ch_t *dma = dma_init(DMA_CH);

    pcm_start();

    // Start the DMA, then TX
    dma_initiate(dma, &audio_cb);
    pcm_tx_on();

    for(unsigned rep = 0; rep < 3; rep++) {
        // Re-rendering before the next pass leaves a gap in the audio
        if(rep > 0) {
            uint32_t start = timer_get_usec();
            render_square();
            printk("re-render %d us\n", timer_get_usec() - start);
            dma_initiate(dma, &audio_cb);
        }

        uint32_t start = timer_get_usec();
        while(dma_active(dma)) {}
        printk("pass %d: %d us\n", rep, timer_get_usec() - start);
    }
}
