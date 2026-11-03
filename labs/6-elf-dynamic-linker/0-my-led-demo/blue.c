// BLUE.SO: LED plugin for the blue LED on GPIO 18
#include "rpi.h"

#define PIN 18

const char *led_name(void) { return "blue"; }
void led_init(void) { gpio_set_output(PIN); }
void led_on(void) { gpio_set_on(PIN); }
void led_off(void) { gpio_set_off(PIN); }
