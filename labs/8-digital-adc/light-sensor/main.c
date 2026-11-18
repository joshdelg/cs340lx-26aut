#include "rpi.h"
#include "gpio-raw.h"
#include "cycle-count.h"
#include "notes.h"

enum { LED_POWER = 16, LED_SENSE = 17 };
enum { TIMEOUT = 420 * 1000 * 1000 };

// Reverse bias LED, floating cathode -- time how long the
// photocurrent takes to drain it
static uint32_t light_read(void) {
    gpio_set_output(LED_SENSE);
    gpio_set_off(LED_SENSE);
    gpio_set_output(LED_POWER);
    gpio_set_on(LED_POWER);
    delay_us(10);

    uint32_t s = cycle_cnt_read();
    gpio_set_input_raw(LED_POWER);
    while(gpio_read_raw(LED_POWER))
        if(cycle_cnt_read() - s > TIMEOUT)
            return ~0;
    return cycle_cnt_read() - s;
}

static uint32_t cal[NNOTES];

static unsigned closest_note(uint32_t t) {
    for(unsigned n = 0; n < NNOTES - 1; n++)
        if((uint64_t)t * t < (uint64_t)cal[n] * cal[n+1])
            return n;
    return NOTE_OFF;
}

void notmain(void) {
    gpio_set_pud_off(LED_POWER);

    for(unsigned n = 0; n < NNOTES; n++) {
        output("hold light at position %d (6 = dark)\n", n + 1);
        delay_ms(5000);
        cal[n] = light_read();
        output("  %d cycles\n", cal[n]);
    }

    notes_init();
    unsigned playing = NOTE_OFF, last = NOTE_OFF;
    while(1) {
        unsigned n = closest_note(light_read());
        if(n == last && n != playing)       // 2 readings agree
            note_play(playing = n);
        last = n;
    }
}
