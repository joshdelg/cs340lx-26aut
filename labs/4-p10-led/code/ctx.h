#pragma once

#include "dma-impl.h"

// =================================================================================================
// SECTION: CONTEXT MANIPULATION
// =================================================================================================

// This is the API I've landed on for writing long-form DMA computations. Basically, it's a bump
// allocator for control blocks. You 'allocate' blocks by 'emit'-ing them (`ctx_emit_*`). Blocks
// automatically point their NEXT_CB to block immediately after them in memory, but this can be
// altered by manually inserting `ctx_end`/`ctx_branch` after emitting a block. The bus address of
// the next block to be emitted can be gotten using `ctx_label` (which is useful not only with
// `ctx_branch` but also `bus_rel_*`). Lastly, a pointer to the last emitted block can be gotten
// with `cb_last`, and a pointer to the next block that will be emitted with `cb_here`.
//
// Feel free to use this API if you feel like it, or do your own thing if you don't.
//
// Notes:
//  - blocks are contiguous in memory, and are emitted in exactly program order
//
//      cb_t *cb_0 = ctx_here(ctx);
//      ctx_emit(ctx, ...);
//      cb_t *cb_1 = ctx_here(ctx);
//      assert(cb_1 == cb_0 + 1);
//  
//    this means that bus_rel_fld(bus(cb_0),+1,FLD_SRC,0) points to the least significant byte
//    of cb_1's SRC_ADDR field.
// 
//  - ctx_emit_* automatically point the NEXT_CB of the emitted control block at the next block

struct {
  cb_t *base;
  cb_t *p;
  size_t capacity;
} typedef ctx_t;

// Get pointer to the location where the next control block will be emitted.
static cb_t *ctx_here(ctx_t *ctx) {
  return ctx->p;
}

// Get pointer to the previously emitted control block.
static cb_t *ctx_last(ctx_t *ctx) {
  assert(ctx->p > ctx->base);
  return ctx->p - 1;
}

// Equivalent to bus(ctx_here(ctx))
//
// Called it `label` because it's similar to setting up labels in an assembly language program.
static bus_t ctx_label(ctx_t *ctx) {
  return bus(ctx->p);
}

// Return 1 if bus(ctx_here(ctx)) == b, otherwise 0.
// 
// Useful for assertions
static _Bool ctx_at(ctx_t *ctx, bus_t b) {
  return bus_eq(bus(ctx->p), b);
}

// Set NEXT_CB of previously emitted control block to 0.
//
// CRUCIAL: This is NOT a block in and of its own right!!!
static void ctx_branch(ctx_t *ctx, bus_t label) {
  ctx_last(ctx)->NEXT_CB = label;
}

// Set NEXT_CB of previously emitted control block to 0.
//
// If the DMA controller reaches that block and the NEXT_CB is still 0, the DMA controller will
// stop executing control blocks after that block's transfer finishes.
// 
// CRUCIAL: This is NOT a block in and of its own right!!!
static void ctx_end(ctx_t *ctx) {
  ctx_branch(ctx, (bus_t){0});
}

// -------------------------------------------------------------------------------------------------
// Initialize `ctx` by `kmalloc`-ing enough memory to fit `n_blocks` control blocks.

static void ctx_init(ctx_t *ctx, size_t n_blocks)
{
  void *blk_base = kmalloc((n_blocks + 1) * sizeof(cb_t));
  uintptr_t delta = 32 - ((uintptr_t )blk_base % 32);
  blk_base += delta;
  assert(is_cb_aligned(blk_base));

  ctx->base = blk_base;
  ctx->p = ctx->base;
  ctx->capacity = n_blocks;
}

// -------------------------------------------------------------------------------------------------
// Generate control blocks for linear transfers.

static void ctx_emit_with(ctx_t *ctx, uint32_t flags, bus_t dst, bus_t src, size_t len) {
  if((ctx->p - ctx->base) >= ctx->capacity)
    panic("ctx_emit_with: ran out of blocks (limit set by ctx_init: %d)\n", ctx->capacity);

  cb_t *cb = ctx->p++;
  __builtin_memset(cb, 0, 32);
  cb->TI = flags;
  cb->DST_ADDR = dst;
  cb->SRC_ADDR = src;
  cb->TXFR_LEN = len;
  cb->NEXT_CB = bus(ctx->p);
}
static void ctx_emit(ctx_t *ctx, bus_t dst, bus_t src, size_t len) {
  // printf("EMIT:%p: %x[0:%d] <- %x[0:%d]\n", cb, dst, len, src, len);
  ctx_emit_with(ctx, DST_INC | SRC_INC, dst, src, len);
}

// -------------------------------------------------------------------------------------------------
// Generate control blocks for 2D transfers.
//
// Notes:
//  - Check the docs!
//  - designed for copying 'rectangular' parts of images around; 2D-mode transfers
//    have an extra STRIDE field to that controls the behaviour after copying each
//    'row', which can be used for scatter/gather kinda stuff.
//  - kinda slow tbh, but can occasionally still help speed up a computation.

static void ctx_emit_strided_with(ctx_t *ctx, uint32_t flags, bus_t dst, bus_t src, uint16_t cnt,
                                  uint16_t len, uint32_t stride) {
  if((ctx->p - ctx->base) >= ctx->capacity)
    panic("ctx_emit_strided_with: ran out of blocks (limit set by ctx_init: %d)\n", ctx->capacity);
  assert(flags & _2DMODE);
  cb_t *cb = ctx->p++;
  __builtin_memset(cb, 0, 32);
  cb->TI = flags;
  cb->DST_ADDR = dst;
  cb->SRC_ADDR = src;
  // Funny feature of _2DMODE: TXFR_LEN.YLENGTH is treated as being 1 higher
  // than it really is
  cnt -= 1;
  cb->TXFR_LEN = ((uint32_t)cnt << 16) | (uint32_t)len;
  cb->NEXT_CB = bus(ctx->p);
  cb->STRIDE = stride;
}
static void ctx_emit_strided(ctx_t *ctx, bus_t dst, bus_t src, uint16_t cnt, uint16_t len, uint32_t stride) {
  // printf("EMIT_STRIDED:%p: %x <- %x (%d x %d+%d)\n", ctx->p, dst, src, cnt,
  // len,
  //        stride);
  ctx_emit_strided_with(ctx, DST_INC | SRC_INC | _2DMODE, dst, src, cnt, len, stride);
}


// -------------------------------------------------------------------------------------------------
// Make it so the next control block to be emitted will have alignment `align`.
//
// If `update_prev` is true, then will update the NEXT_CB of the previously
// emitted control block.
//
// Occasionally useful for doing tricks with NEXT_CB: _Alignas is easy to use
// for SRC/DST stuf; this is the equivalent tool for NEXT_CB.

static void ctx_skip_to_align(ctx_t *ctx, size_t align, _Bool update_prev) {
  assert(align >= 32);
  // assert that alignment is a power of 2
  assert((align & (align - 1)) == 0);

  uintptr_t curr = (uintptr_t)ctx->p;
  size_t next_aligned = (curr + (align - 1)) & ~(align - 1);
  size_t alignment_delta = next_aligned - curr;
  cb_t *new_p = ctx->p + (alignment_delta / 32);
  if((new_p - ctx->base) >= ctx->capacity)
    panic("ctx_skip_to_align: ran out of blocks (limit set by ctx_init: %d)\n", ctx->capacity);
  if(update_prev)
    ctx_branch(ctx, bus(new_p));
  ctx->p = new_p;
}


