#include "rpi.h"
#include "gpio.h"

#include "pcm.h"

void notmain() {
    for(unsigned i = 0; i < sizeof(PCM_PINS) / sizeof(unsigned); i++) {
        gpio_set_function(PCM_PINS[i], GPIO_FUNC_ALT0);
    }

    pcm_start();

    // 440Hz square wave, 3 seconds
    uint32_t start = timer_get_usec();
    for(unsigned rep = 0; rep < 3; rep++) {
        for(uint32_t i = 0; i < 48000; i++) {
            int32_t sample = ((i * 440) % 48000 < 24000) ? (1 << 28) : -(1 << 28);
            pcm_write(sample); // left
            pcm_write(sample); // right
        }
    }
    printk("took %d us\n", timer_get_usec() - start);
}
