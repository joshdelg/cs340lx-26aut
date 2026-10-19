#include "rpi.h"
#include "dma-impl.h"
#include "spi-util.h"
#include "ctx.h"

#define WIDTH           32
#define HEIGHT          16
#define BYTES_PER_ROW   (WIDTH / 8)
#define NGROUPS         4
#define BYTES_PER_GROUP (WIDTH * HEIGHT / 8 / NGROUPS)

#define PERIOD_US   2000
#define ON_US       20

static uint8_t fb[HEIGHT][BYTES_PER_ROW];
// Pad and align so DMA can write g to Byte 1 to address
static volatile _Alignas(256 * 256) uint8_t packed_fb[NGROUPS][256];
// static volatile uint8_t group = 0;

// scan pattern: group g drives rows g, 4+g, 8+g, 12+g
// within a column bottom to top
// 0 = on so invert here.
static void pack_groups() {
    for(unsigned g = 0; g < NGROUPS; g++) {
        unsigned k = 0;
        for(unsigned col = 0; col < BYTES_PER_ROW; col++)
            for(int q = 3; q >= 0; q--)
                packed_fb[g][k++] = ~fb[q * 4 + g][col];
    }
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

// -------- DMA Programming Helpers --------

// [i] = (i + 1) % 256
static volatile _Alignas(256) uint8_t INC8_MOD4_TAB[256];
// Pad and align so we can access it by writing g to byte 1
static volatile _Alignas(256 * 256) uint32_t GPIO_ABSET_TAB[NGROUPS][64];

static volatile uint32_t AB_CLR = (1 << ADD_A) | (1 << ADD_B);
static volatile uint32_t SCLK_MASK = 1 << SCLK;
static volatile uint32_t OE_MASK = 1 << OE;

// SPI frame control: TA 0->1 starts a new frame of DLEN bytes
static volatile uint32_t SPI_CS_IDLE = CS_DMAEN;
static volatile uint32_t SPI_CS_GO   = CS_DMAEN | CS_TA;
static volatile uint32_t SPI_DLEN_VAL = BYTES_PER_GROUP;

static void init_tables(void) {
  for(int i = 0; i < 256; i++)
    INC8_MOD4_TAB[i] = (i + 1) % 4;

  for(int g = 0; g < 4; g++) {
    GPIO_ABSET_TAB[g][0] = ((g & 0x1) << ADD_A) | (((g >> 1) & 0x1) << ADD_B);
  }
}

// *out = (*in + 1) % 256 
static cb_t *emit_inc8_mod4(ctx_t *ctx, bus_t out, bus_t in) {
  cb_t *first = ctx_here(ctx);
  ctx_emit(ctx, bus_rel_fld(bus(first), 1, FLD_SRC, 0), in, 1);
  ctx_emit(ctx, out, bus(INC8_MOD4_TAB), 1);
  return first;
}

// Busy-wait ~<ms> milliseconds (1 block), using the same TI flags as cb_mk_delayed.
static cb_t *emit_delay_ms(ctx_t *ctx, unsigned ms) {
  cb_t *first = ctx_here(ctx);
  size_t len = ms * DMA_MS_DELAY_BYTES;
  cb_t tmpl = cb_mk_delayed(bus(&dma_delay_dummy_dst), bus(&dma_delay_dummy_src), len);
  ctx_emit_with(ctx, tmpl.TI, tmpl.DST_ADDR, tmpl.SRC_ADDR, len);
  return first;
}

enum { DMA_US_DELAY_BYTES_X1000 = DMA_MS_DELAY_BYTES };  // 11872 bytes per 1000 us

// emit a delay block into the chain (don't dma_run it: it lives in the ring)
static void ctx_emit_delay_us(ctx_t *ctx, unsigned us) {
    unsigned len = (us * DMA_US_DELAY_BYTES_X1000) / 1000;   // 20us -> 237 bytes
    if(len == 0) len = 1;
    ctx_emit_with(ctx,
        (WAITS_MAX << WAITS_OFFSET) | NO_WIDE_BURSTS | WAIT_RESP,  // same as cb_mk_delayed
        bus(&dma_delay_dummy_dst), bus(&dma_delay_dummy_src), len);
}

static cb_t* emit_tx_dma_chain() {
    ctx_t ctx;
    ctx_init(&ctx, 64);

    static volatile unsigned group = 0;

    // Start a new SPI frame: end the previous one, set its length, re-activate
    cb_t *loop = ctx_here(&ctx);
    ctx_emit(&ctx, bus((void *)SPI0_CS), bus(&SPI_CS_IDLE), 4);
    ctx_emit(&ctx, bus((void *)SPI0_DLEN), bus(&SPI_DLEN_VAL), 4);
    ctx_emit(&ctx, bus((void *)SPI0_CS), bus(&SPI_CS_GO), 4);

    // patch offsets below are relative to start
    cb_t *start = ctx_here(&ctx);

    ctx_emit(&ctx, bus((void *)GPIO_CLR0), bus(&AB_CLR), 4); // Clear A and B
    
    // Set A and B properly: Load g into byte 1 of set block's source
    ctx_emit(&ctx, bus_rel_fld(bus(start), 2, FLD_SRC, 1), bus(&group), 1);
    ctx_emit(&ctx, bus((void *)GPIO_SET0), bus(GPIO_ABSET_TAB), 4);

    // Load framebuffer address into TX DMA block
    ctx_emit(&ctx, bus_rel_fld(bus(start), 4, FLD_SRC, 1), bus(&group), 1); // Load g into next block
    // Load fb into fifo: SRC walks the buffer, DST stays on the FIFO register, paced by SPI TX DREQ
    ctx_emit_with(&ctx, SRC_INC | DST_DREQ | WAIT_RESP | (DREQ_SPI_TX << PERMAP_OFFSET),
                  bus((void *)SPI0_FIFO), bus(packed_fb), BYTES_PER_GROUP);

    // Pulse SCLK
    ctx_emit_delay_us(&ctx, 500);
    ctx_emit(&ctx, bus((void *)GPIO_SET0), bus(&SCLK_MASK), 4);
    ctx_emit_delay_us(&ctx, 500);
    ctx_emit(&ctx, bus((void *)GPIO_CLR0), bus(&SCLK_MASK), 4);

    // Pulse OE
    ctx_emit(&ctx, bus((void *)GPIO_SET0), bus(&OE_MASK), 4);
    ctx_emit_delay_us(&ctx, ON_US);
    ctx_emit(&ctx, bus((void *)GPIO_CLR0), bus(&OE_MASK), 4);
    ctx_emit_delay_us(&ctx, PERIOD_US - ON_US);

    // Add
    ctx_emit(&ctx, bus_rel_fld(bus(start), 14, FLD_SRC, 0), bus(&group), 1);
    ctx_emit(&ctx, bus(&group), bus(INC8_MOD4_TAB), 1);
    ctx_branch(&ctx, bus(loop));

    return loop;
}

void notmain(void) {
    draw_border();
    pack_groups();

    kmalloc_init();

    unsigned pins[] = { OE, ADD_A, ADD_B, SCLK };
    for(unsigned i = 0; i < sizeof pins / sizeof pins[0]; i++) {
        gpio_set_output(pins[i]);
        gpio_set_off(pins[i]);
    }

    init_tables();

    // Init DMA blocks
    dma_ch_t *dma_rx = dma_init(4);
    dma_ch_t *dma_tx = dma_init(5);

    // Init SPI
    spi_init_p10_dma(SPI_CLK_DIV, BYTES_PER_GROUP);

    // Set up RX to drain from SPI RX Fifo
    static uint32_t dummy = 0; 
    static cb_t rx_block;
    rx_block = cb_mk_with(
        bus(&dummy),
        bus((void *) SPI0_FIFO),
        BYTES_PER_GROUP,
        DST_IGNORE | SRC_DREQ | (DREQ_SPI_RX << PERMAP_OFFSET) | WAIT_RESP
    );
    rx_block.NEXT_CB = bus(&rx_block);

    // Start RX DMA
    dma_initiate(dma_rx, &rx_block);

    cb_t *tx_dma_chain = emit_tx_dma_chain();
    dma_initiate(dma_tx, tx_dma_chain);

    output("Finished setting up DMA chain.\n");

    while(1) {
        // printk("tx: CS=%x CB=%x DBG=%x | rx: CS=%x CB=%x | spi CS=%x DLEN=%d grp=%d\n",
        //     dma_tx->CS, dma_tx->CB_ADDR._0, dma_tx->DEBUG,
        //     dma_rx->CS, dma_rx->CB_ADDR._0,
        //     GET32(SPI0_CS), GET32(SPI0_DLEN), group);   // make `group` file-scope to print it
        // delay_ms(500);
    }

}