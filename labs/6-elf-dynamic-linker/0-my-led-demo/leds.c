// LEDS.ELF: uses the plugin abstraction
#include "rpi.h"
#include "dlfcn.h"

// Funcs implemented by .so's
typedef struct {
    const char *(*name)(void);
    void (*init)(void);
    void (*on)(void);
    void (*off)(void);
} led_plugin_t;

// Returns 1 if `file` loaded and has the whole plugin interface, else 0
static int load_plugin(const char *file, led_plugin_t *p) {
    void *h = dlopen(file, RTLD_NOW);
    if (!h)
        return 0;
    p->name = dlsym(h, "led_name");
    p->init = dlsym(h, "led_init");
    p->on = dlsym(h, "led_on");
    p->off = dlsym(h, "led_off");
    return p->name && p->init && p->on && p->off;
}

void notmain() {
    const char *plugins[] = { "YELLOW.SO", "BLUE.SO", "BOTH.SO" };
    unsigned nplugins = sizeof plugins / sizeof plugins[0];

    for (int round = 0; round < 4; round++) {
        for (unsigned i = 0; i < nplugins; i++) {
            led_plugin_t p;
            if (!load_plugin(plugins[i], &p)) {
                printk("leds: no %s, skipping\n", plugins[i]);
                continue;
            }
            printk("leds: %s\n", p.name());
            p.init();
            p.on();
            delay_ms(700);
            p.off();
            delay_ms(300);
        }
    }
    clean_reboot();
}
