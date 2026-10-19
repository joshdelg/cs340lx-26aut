// similar to 4-dma-inc, but now we're adding together two numbers
//
// Note that this file has three parts in it, so it's normal for it to panic at first

#include "rpi.h"
#include "dma-impl.h"
#include "ctx.h"

void _putchar(char c) { uart_put8(c); }

// [rhs][lhs]
static volatile _Alignas(256 * 256) uint8_t ADD8_TAB[256][256];
static volatile _Alignas(256 * 256) uint8_t ADD8_CARRY_TAB[256][256];

// =================================================================================================
// Part 1. Figure out how to do 8-bit addition.

// Goal:
//
//   *out8 = (*lhs8 + *rhs8) % 256
cb_t *stamp_add8(ctx_t *ctx, bus_t out8, bus_t lhs8, bus_t rhs8) {
  // Think about how to expand what you did in `stamp_inc8` to work on multiple inputs
  //
  // In particular:
  //  - mechanically, how did you index into the table?
  //  - can you expand that to work for multiple inputs?

  cb_t *add8_first_cb_addr = ctx_here(ctx);
  
  // RHS copied into byte 1
  ctx_emit(ctx, bus_rel_fld(bus(add8_first_cb_addr), 2, FLD_SRC, 1), rhs8, 1);
  ctx_emit(ctx, bus_rel_fld(bus(add8_first_cb_addr), 2, FLD_SRC, 0), lhs8, 1);
  ctx_emit(ctx, out8, bus(ADD8_TAB), 1);

  return add8_first_cb_addr;
}

void test_add8(dma_ch_t *dma, ctx_t *ctx) {
  volatile uint8_t out, lhs, rhs;

  cb_t *program_add8 = stamp_add8(ctx, bus(&out), bus(&lhs), bus(&rhs));
  ctx_end(ctx);
  
  uint8_t TESTS[][2] = {
    // lhs, rhs
    {0x00,0x00},
    {0x01,0x00},
    {0x00,0x01},
    {0xff,0xff},
    {0xff,0x01},
    {0x47,0x56},
  };
  size_t num_tests = sizeof TESTS / sizeof *TESTS;

  for(int i=0;i<num_tests;i++) {
    enum { TIMEOUT = 1024 };
    lhs = TESTS[i][0];
    rhs = TESTS[i][1];
    uint8_t expected = lhs + rhs;
    out = 0;

    dma_initiate(dma, program_add8);
    if(!dma_wait(dma,TIMEOUT)) panic("dma timed out\n");

    if(out != expected || lhs != TESTS[i][0] || rhs != TESTS[i][1])
      panic("didn't add: out=%x, lhs=%x, rhs=%x, expected=%x\n", out, lhs, rhs, expected);
  }

  output("SUCCESS!  8-bit addition works on all test inputs!\n");
}

// =================================================================================================
// Part 2. 8-bit addition with carry

cb_t *stamp_add8_carry(ctx_t *ctx, bus_t out8, bus_t carry8, bus_t lhs8, bus_t rhs8) {
  // Basically the same as stamp_add8, except you need an extra table
  // You want to write the carry to `carry8`

  cb_t *add8_first_cb_addr = ctx_here(ctx);
  
  // RHS copied into byte 1
  ctx_emit(ctx, bus_rel_fld(bus(add8_first_cb_addr), 4, FLD_SRC, 1), rhs8, 1);
  ctx_emit(ctx, bus_rel_fld(bus(add8_first_cb_addr), 5, FLD_SRC, 1), rhs8, 1);

  ctx_emit(ctx, bus_rel_fld(bus(add8_first_cb_addr), 4, FLD_SRC, 0), lhs8, 1);
  ctx_emit(ctx, bus_rel_fld(bus(add8_first_cb_addr), 5, FLD_SRC, 0), lhs8, 1);

  ctx_emit(ctx, out8, bus(ADD8_TAB), 1);
  ctx_emit(ctx, carry8, bus(ADD8_CARRY_TAB), 1);

  return add8_first_cb_addr;
}

void test_add8_carry(dma_ch_t *dma, ctx_t *ctx) {
  volatile uint8_t out, carry, lhs, rhs;

  cb_t *program_add8_carry = stamp_add8_carry(ctx, bus(&out), bus(&carry), bus(&lhs), bus(&rhs));
  ctx_end(ctx);
  
  uint8_t TESTS[][2] = {
    /// lhs, rhs
    {0x00,0x00},
    {0x01,0x00},
    {0x00,0x01},
    {0xff,0xff},
    {0xff,0x01},
    {0x47,0x56},
  };
  size_t num_tests = sizeof TESTS / sizeof *TESTS;

  for(int i=0;i<num_tests;i++) {
    enum { TIMEOUT = 1024 };
    lhs = TESTS[i][0];
    rhs = TESTS[i][1];
    uint16_t expected = (uint16_t)lhs + (uint16_t)rhs;
    out = 0;
    carry = 0;

    dma_initiate(dma, program_add8_carry);
    if(!dma_wait(dma,TIMEOUT)) panic("dma timed out\n");

    if(out != (expected&0xff) || carry != (expected>>8) || lhs != TESTS[i][0] || rhs != TESTS[i][1])
      panic("didn't add: out=%x, lhs=%x, rhs=%x, expected=%x\n", out, lhs, rhs, expected);
  }

  output("SUCCESS!  8-bit addition with carry works on all test inputs!\n");
}


// =================================================================================================
// Part 3. 32-bit addition

cb_t *stamp_add32(ctx_t *ctx, bus_t out32, bus_t lhs32, bus_t rhs32) {
  // once you have stamp_add8_carry, you can already write stamp_add32 by doing the addition you
  // learned in elementary school in base-256.
  //
  // Useful:
  //  - Ripple-Carry Adder: https://en.wikipedia.org/wiki/Adder_(electronics)
  //  - Note that stamp_add8_carry is an 8-bit half adder; you'll need to figure how to make an
  //    8-bit full adder and then chain the 8-bit full adders into a ripple-carry adder
  //  - it's helpful to remember that in addition, the carried value is always either 0 or 1,
  //    and adding together two carry values NEVER results in a carry of 1
  
  static volatile uint8_t buffered_sums[4];
  static volatile uint8_t buffered_carries[4];
  static volatile uint8_t buffered_carries_prime[4];
  static volatile uint8_t buffered_carries_double_prime[4];

  // First byte: L[0] + R[0] = S[0], C[0]
  cb_t *stamp_add32_first_addr = stamp_add8_carry(ctx, out32, bus(&buffered_carries[0]), lhs32, rhs32);
  
  // Second byte
  // -> L[1] + R[1] = S'[1], C'[1]
  stamp_add8_carry(ctx, bus(&buffered_sums[1]), bus(&buffered_carries_prime[1]), bus_add(lhs32, 1), bus_add(rhs32, 1));
  // -> S[1]' + C[0] = S[1], C''[1]
  stamp_add8_carry(ctx, bus_add(out32, 1), bus(&buffered_carries_double_prime[1]), bus(&buffered_sums[1]), bus(&buffered_carries[0]));
  // -> C[1] = C'[1] + C''[1]
  stamp_add8(ctx, bus(&buffered_carries[1]), bus(&buffered_carries_prime[1]), bus(&buffered_carries_double_prime[1]));
  
  // Third byte
  // -> L[2] + R[2] = S'[2], C'[2]
  stamp_add8_carry(ctx, bus(&buffered_sums[2]), bus(&buffered_carries_prime[2]), bus_add(lhs32, 2), bus_add(rhs32, 2));
  // -> S'[2] + C[1] = S[2], C''[2]
  stamp_add8_carry(ctx, bus_add(out32, 2), bus(&buffered_carries_double_prime[2]), bus(&buffered_sums[2]), bus(&buffered_carries[1]));
  // -> C[2] = C'[2] + C''[2]
  stamp_add8(ctx, bus(&buffered_carries[2]), bus(&buffered_carries_prime[2]), bus(&buffered_carries_double_prime[2]));

  // Fourth byte: carry out of the top byte is discarded, so only sums are needed
  // -> L[3] + R[3] = S'[3]
  stamp_add8(ctx, bus(&buffered_sums[3]), bus_add(lhs32, 3), bus_add(rhs32, 3));
  // -> S'[3] + C[2] = S[3]
  stamp_add8(ctx, bus_add(out32, 3), bus(&buffered_sums[3]), bus(&buffered_carries[2]));

  return stamp_add32_first_addr;
}

void test_add32(dma_ch_t *dma, ctx_t *ctx) {
  volatile uint32_t out, lhs, rhs;

  cb_t *program_add32 = stamp_add32(ctx, bus(&out), bus(&lhs), bus(&rhs));
  ctx_end(ctx);
  
  uint32_t TESTS[][2] = {
    // lhs, rhs
    {0,0},
    {1,0},
    {0,1},
    {0xffffffff,0xffffffff},
    {0xffffffff,1},
    {0x12345678,0x9abcdef0},
  };
  size_t num_tests = sizeof TESTS / sizeof *TESTS;

  for(int i=0;i<num_tests;i++) {
    enum { TIMEOUT = 1024 };
    lhs = TESTS[i][0];
    rhs = TESTS[i][1];
    uint32_t expected = lhs + rhs;
    out = 0;

    dma_initiate(dma, program_add32);
    if(!dma_wait(dma,TIMEOUT)) panic("dma timed out\n");

    if(out != expected || lhs != TESTS[i][0] || rhs != TESTS[i][1])
      panic("didn't add: out=%x, lhs=%x, rhs=%x, expected=%x\n", out, lhs, rhs, expected);
  }

  output("SUCCESS!  32-bit addition works on all test inputs!\n");
}

void notmain() {
  kmalloc_init();

  // Init tables
  for(unsigned rhs = 0; rhs < 256; rhs++) {
    for(unsigned lhs = 0; lhs < 256; lhs++) {
      ADD8_TAB[rhs][lhs] = (rhs + lhs) % 256;
      ADD8_CARRY_TAB[rhs][lhs] = (rhs + lhs) >= 256;
    }
  }

  enum {
    DMA_CH = 4,
  };
  dma_ch_t *dma = dma_init(DMA_CH);

  ctx_t ctx;
  ctx_init(&ctx, 1024);

  test_add8(dma, &ctx);
  test_add8_carry(dma, &ctx);
  test_add32(dma, &ctx);

  output("SUCCESS!  All parts passed!\n");
}
