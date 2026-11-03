// YELLOW.SO: LED plugin for the yellow LED on GPIO 16
#include "rpi.h"

#define PIN 16

const char *led_name(void) { return "yellow"; }
void led_init(void) { gpio_set_output(PIN); }
void led_on(void) { gpio_set_on(PIN); }
void led_off(void) { gpio_set_off(PIN); }
