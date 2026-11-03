// BOTH.SO: LED plugin to flash both
#include "rpi.h"

#define YELLOW 16
#define BLUE 18

const char *led_name(void) { return "both"; }
void led_init(void) { gpio_set_output(YELLOW); gpio_set_output(BLUE); }
void led_on(void) { gpio_set_on(YELLOW); gpio_set_on(BLUE); }
void led_off(void) { gpio_set_off(YELLOW); gpio_set_off(BLUE); }
