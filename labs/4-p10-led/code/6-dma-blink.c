// After 3-dma-blink, the only thing stopping us from doing the entire final for loop in DMA is that
// we couldn't do the `for(int i=0;i<N;i++)` in DMA. However, we just learned to do addition, so
// the only thing left is to figure out branching :)

#include "rpi.h"
#include "dma-impl.h"
#include "ctx.h"

// Feel free to put your own helpers in here :)

void _putchar(char c) { uart_put8(c); }

// @joshdelg Helpers from the earlier files, as emitted control blocks rather than running directly

// [i] = (i + 1) % 256
static volatile _Alignas(256) uint8_t INC8_TAB[256];

static void init_tables(void) {
  for(int i = 0; i < 256; i++)
    INC8_TAB[i] = (i + 1) % 256;
}

// *out = (*in + 1) % 256 
static cb_t *emit_inc8(ctx_t *ctx, bus_t out, bus_t in) {
  cb_t *first = ctx_here(ctx);
  ctx_emit(ctx, bus_rel_fld(bus(first), 1, FLD_SRC, 0), in, 1);
  ctx_emit(ctx, out, bus(INC8_TAB), 1);
  return first;
}

// Pin mask lives on the heap so it stays valid while the DMA runs.
static bus_t pin_mask(unsigned pin) {
  volatile uint32_t *mask = kmalloc(sizeof *mask);
  *mask = 1 << pin;
  return bus(mask);
}

// GPIO SET0/CLR0 write (1 block each)
static cb_t *emit_gpio_on(ctx_t *ctx, unsigned pin) {
  cb_t *first = ctx_here(ctx);
  ctx_emit(ctx, bus((void *)GPIO_SET0), pin_mask(pin), 4);
  return first;
}

static cb_t *emit_gpio_off(ctx_t *ctx, unsigned pin) {
  cb_t *first = ctx_here(ctx);
  ctx_emit(ctx, bus((void *)GPIO_CLR0), pin_mask(pin), 4);
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

cb_t *dma_blink(ctx_t *ctx, unsigned pin, unsigned delay, bus_t n_times) {
  // NOTE: n_times is a pointer! can't unroll

  // Goal: should be equivalent to the for(i=0;i<N;i++) { on ; delay ; off ; delay;  }
  //
  // It's useful to break this down to pseudo-assembly:
  //
  // loop:
  //     if i = N { goto end } <- this is the part you need to figure out
  //     i = i + 1
  //     on
  //     delay
  //     off
  //     delay
  //     goto loop <- you can use NEXT_CB to get this effect
  // end:
  //     exit <- this can be a no-op control block that has NEXT_CB=0
  // 
  // Useful for conditionals:
  //  - basic idea is https://en.wikipedia.org/wiki/Branch_table
  //  - similar to what you've done before, except instead of manipulating the SRC_ADDR of another
  //    block, you're manipulating the NEXT_CB!
  //  - you can assume n_times<256 if it helps
  //  - you might need to use multiple tables
  
  static volatile uint8_t counter = 0;
  static volatile uint8_t zero = 0;

  // [N, i] = first byte of exit node on diagonal, first byte of loop body off diagonal
  static volatile _Alignas(256 * 256) uint8_t BRANCH_TAB[256][256];

  // Entry: reset counter to 0
  cb_t *ctr_reset = ctx_here(ctx);
  ctx_emit(ctx, bus(&counter), bus(&zero), 1);

  // Check: Look up tab[N][i] and patch address
  cb_t *check = ctx_here(ctx);
  ctx_emit(ctx, bus_rel_fld(bus(check), 2, FLD_SRC, 0), bus(&counter), 1);
  ctx_emit(ctx, bus_rel_fld(bus(check), 2, FLD_SRC, 1), n_times, 1);
  ctx_emit(ctx, bus_rel_fld(bus(check), 3, FLD_NXT, 0), bus(BRANCH_TAB), 1);

  // Brach: dummy no op to act as branch node
  ctx_emit(ctx, bus(&zero), bus(&zero), 1);

  // Body: Align 256 so starts at 0x..00 so Exit and Inc8 can be indexed by their lowest byte
  ctx_skip_to_align(ctx, 256, 1);
  cb_t *aligned = ctx_here(ctx);
  
  // Exit: no op + ctx_end
  ctx_emit(ctx, bus(&zero), bus(&zero), 1);
  ctx_end(ctx);

  // Blink: Inc, on, delay, off, delay
  cb_t *inc_start = emit_inc8(ctx, bus(&counter), bus(&counter));
  emit_gpio_on(ctx, pin);
  emit_delay_ms(ctx, delay);
  emit_gpio_off(ctx, pin);
  emit_delay_ms(ctx, delay);

  // Branch to check
  ctx_branch(ctx, bus(check));


  // Fill in branch table
  for(unsigned n = 0; n < 256; n++) {
    for(unsigned i = 0; i < 256; i++) {
      if(n == i) BRANCH_TAB[n][i] = (((uint32_t) aligned) & 0xff);
      else BRANCH_TAB[n][i] = (((uint32_t) inc_start) & 0xff);
    }
  }

  return ctr_reset;
}

void notmain() {
  kmalloc_init();
  init_tables();

  enum { DMA_CH = 4 };
  dma_ch_t *dma = dma_init(DMA_CH);

  ctx_t ctx;
  ctx_init(&ctx, 1024);

  enum { pin = 27, delay = 500 };
  gpio_set_output(pin);
  volatile uint8_t N = 5;
  cb_t *program_blink = dma_blink(&ctx, pin, delay, bus(&N));

  dma_initiate(dma, program_blink);


  // should take about 5s; round it up to 10
  enum { TIMEOUT_MICROS = 10 * 1000 * 1000 };
  uint32_t t_start = timer_get_usec();
  while(!dma_done(dma)) {
    if ((timer_get_usec() - t_start) >= TIMEOUT_MICROS)
      panic("dma timed out after %d seconds!\n", TIMEOUT_MICROS / 1000 / 1000);
  }
}
