#include "rpi.h"
#include "spi-util.h"

#define WIDTH           32
#define HEIGHT          16
#define BYTES_PER_ROW   (WIDTH / 8)
#define NGROUPS         4
#define BYTES_PER_GROUP (WIDTH * HEIGHT / 8 / NGROUPS)

// How long to flash
#define PERIOD_US   2000
#define ON_US       20

// logical framebuffer: fb[row][byte col], 1 = LED on, MSB = leftmost px.
static uint8_t fb[HEIGHT][BYTES_PER_ROW];

static void fb_clear(void) {
    for(unsigned y = 0; y < HEIGHT; y++)
        for(unsigned c = 0; c < BYTES_PER_ROW; c++)
            fb[y][c] = 0;
}

// Pixels and bit positions are reversed
// Bit 7 (MSB) = Pixel 0
static void fb_set(unsigned x, unsigned y, int on) {
    if(x >= WIDTH || y >= HEIGHT)
        return;
    uint8_t mask = 0x80 >> (x % 8);
    if(on)
        fb[y][x / 8] |= mask;
    else
        fb[y][x / 8] &= ~mask;
}

// scan pattern: group g drives rows g, 4+g, 8+g, 12+g
// within a column bottom to top
// 0 = on so invert here.
static void pack_group(uint8_t out[BYTES_PER_GROUP], unsigned g) {
    unsigned k = 0;
    for(unsigned col = 0; col < BYTES_PER_ROW; col++)
        for(int q = 3; q >= 0; q--)
            out[k++] = ~fb[q * 4 + g][col];
}

static inline void set_group(unsigned g) {
    gpio_write(ADD_A, g & 1);
    gpio_write(ADD_B, (g >> 1) & 1);
}

// Pulse SCLK
static inline void latch(void) {
    gpio_set_on(SCLK);
    gpio_set_off(SCLK);
}

static void p10_init(void) {
    // CLK and DATA are now driven by SPI0.
    unsigned pins[] = { OE, ADD_A, ADD_B, SCLK };
    for(unsigned i = 0; i < sizeof pins / sizeof pins[0]; i++) {
        gpio_set_output(pins[i]);
        gpio_set_off(pins[i]);
    }
    spi_init_p10(SPI_CLK_DIV);
}

// one full refresh: all four row groups.
static void p10_refresh(void) {
    uint8_t buf[BYTES_PER_GROUP];

    for(unsigned g = 0; g < NGROUPS; g++) {
        pack_group(buf, g);

        // display is off (EN low) while we shift + switch rows, so we
        // don't ghost the previous group's data onto the new rows.
        spi_send_poll(buf, BYTES_PER_GROUP);

        set_group(g);
        latch();

        gpio_set_on(OE);
        delay_us(ON_US);
        gpio_set_off(OE);
        delay_us(PERIOD_US - ON_US);
    }
}

static void draw_border(void) {
    for(unsigned x = 0; x < WIDTH; x++) {
        fb_set(x, 0, 1);
        fb_set(x, HEIGHT - 1, 1);
    }
    for(unsigned y = 0; y < HEIGHT; y++) {
        fb_set(0, y, 1);
        fb_set(WIDTH - 1, y, 1);
    }
}

void notmain(void) {
    p10_init();
    fb_clear();
    draw_border();

    while(1) p10_refresh();
}
