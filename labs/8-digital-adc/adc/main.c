#include "rpi.h"
#include "gpio.h"
#include "gpio-raw.h"
#include "cycle-count.h"
#include "notes.h"

enum { DATA = 23, GND = 25 };

enum {
    DEG0 = 1741,
    DEG90 = 1170,
    DEG180 = 562,
    DEG270 = 268,
};

enum {
    NOTE1 = 1487,
    NOTE2 = 808,
    NOTE3 = 448,
    NOTE4 = 353,
    NOTE5 = 151
};

static const uint32_t note_counts[] = { NOTE1, NOTE2, NOTE3, NOTE4, NOTE5 };

static unsigned closest_note(uint32_t count) {
    unsigned best = 0;
    uint32_t best_dist = ~0;
    for(unsigned i = 0; i < 5; i++) {
        uint32_t d = count > note_counts[i] ? count - note_counts[i] : note_counts[i] - count;
        if(d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

uint32_t count_cycles() {
    gpio_set_output(DATA);
    gpio_set_off_raw(DATA);

    uint32_t s = cycle_cnt_read();
    gpio_set_input_raw(DATA);
    while(gpio_read_raw(DATA) == 0) {}
    uint32_t e = cycle_cnt_read();
    output("Time to rise: %d cycles\n", e - s);
    return e - s;
}

static uint32_t playing = NOTE_OFF;

void notmain() {
    gpio_set_pud_off(DATA);
    gpio_set_pud_off(GND);

    // count_cycles();
    notes_init();

    while(1) {
        uint32_t count = count_cycles();
        uint32_t note = closest_note(count);
        note_play(note);
        delay_ms(1000);
        note_play(NOTE_OFF);
        delay_ms(1000);
    }
}