#include <stdbool.h>
#include "rpi.h"

enum {
    PCM_CLK = 18,
    PCM_FS,
    PCM_DIN,
    PCM_DOUT
};

static unsigned PCM_PINS[4] = {
    PCM_CLK, PCM_FS, PCM_DIN, PCM_DOUT
};

enum {
    PCM_BASE_ADDR = 0x20203000,
    PCM_CS_A = PCM_BASE_ADDR + 0x0,
    PCM_FIFO_A = PCM_BASE_ADDR + 0x4,
    PCM_MODE_A = PCM_BASE_ADDR + 0x8,
    PCM_TXC_A = PCM_BASE_ADDR + 0x10,
    PCM_DREQ_A = PCM_BASE_ADDR + 0x14
};

// CS_A Reg
enum {
    PCM_CS_A_EN_OFFSET = 0,
    PCM_CS_A_TXON_OFFSET = 2,
    PCM_CS_A_TXCLR_OFFSET = 3,
    PCM_CS_A_RXCLR_OFFSET = 4,
    PCM_CS_A_DMAEN_OFFSET = 9,
    PCM_CS_A_TXSYNC_OFFSET = 13,
    PCM_CS_A_TXERR_OFFSET = 15,
    PCM_CS_A_RXERR_OFFSET = 16,
    PCM_CS_A_TXD_OFFSET = 19,
    PCM_CS_A_SYNC_OFFSET = 24,
    PCM_CS_A_STBY_OFFSET = 25
};

// DREQ_A Reg
enum {
    PCM_DREQ_RX_OFFSET = 0,
    PCM_DREQ_TX_OFFSET = 8,
    PCM_DREQ_RX_PANIC_OFFSET = 16,
    PCM_DREQ_TX_PANIC_OFFSET = 24
};

// MODE_A Reg
enum {
    PCM_FSLEN_OFFSET = 0,
    PCM_FLEN_OFFSET = 10,
    PCM_FSI_OFFSET = 20,
    PCM_CLKI_OFFSET = 22
};

// TXC_A Reg
enum {
    PCM_CH2WID_OFFSET = 0,
    PCM_CH2POS_OFFSET = 4,
    PCM_CH2EN_OFFSET = 14,
    PCM_CH2WEX_OFFSET = 15,
    PCM_CH1WID_OFFSET = 16,
    PCM_CH1POS_OFFSET = 20,
    PCM_CH1EN_OFFSET = 30,
    PCM_CH1WEX_OFFSET = 31,
};

// Clock Config
enum {
    CM_PCM_CTRL = 0x20101098, // 0x7E101098
    CM_PCM_CTRL_CLOCK_SOURCE_OFFSET = 0,
    CM_PCM_CTRL_CLOCK_ENAB_OFFSET = 4,
    CM_PCM_CTRL_CLOCK_MASH_OFFSET = 9,
    CM_PCM_CTRL_CLOCK_PASSWD_OFFSET = 24,
    // ---- //
    CM_PCM_DIV = 0x2010109C,
    CM_PCM_DIV_DIVF_OFFSET = 0,
    CM_PCM_DIV_DIVI_OFFSET = 12,
    CM_PCM_DIV_PASSWD_OFFSET = 24,

    CM_PCM_SRC_PLLD = 0b0110, // runs at a 500MHz nominal
    CM_PCM_PASSWD = 0x5A
};

static void pcm_configure_clock() {
    uint32_t clock_ctrl = 0;
    clock_ctrl |= (CM_PCM_SRC_PLLD << CM_PCM_CTRL_CLOCK_SOURCE_OFFSET);
    clock_ctrl |= (1 << CM_PCM_CTRL_CLOCK_ENAB_OFFSET);
    clock_ctrl |= (0b01 << CM_PCM_CTRL_CLOCK_MASH_OFFSET);
    clock_ctrl |= (CM_PCM_PASSWD << CM_PCM_CTRL_CLOCK_PASSWD_OFFSET);

    uint32_t clock_div = 0;
    clock_div |= (162 << CM_PCM_DIV_DIVI_OFFSET);
    clock_div |= (3115 << CM_PCM_DIV_DIVF_OFFSET);
    clock_div |= (CM_PCM_PASSWD << CM_PCM_DIV_PASSWD_OFFSET);

    // Divider first, so the clock never runs with a stale DIV
    PUT32(CM_PCM_DIV, clock_div);
    PUT32(CM_PCM_CTRL, clock_ctrl);
}

static void pcm_init() {
    // 2 32-bit changes
    uint32_t mode_a = 0;
    mode_a |= (32 << PCM_FSLEN_OFFSET); // Each channel 32 clocks
    mode_a |= (63 << PCM_FLEN_OFFSET); // Total bits is 63 + 1
    
    // Invert clock since DAC expects data on falling edge
    mode_a |= (1 << PCM_CLKI_OFFSET);

    // Invert frame sync, since we start the frame on falling edge
    mode_a |= (1 << PCM_FSI_OFFSET);

    // Set both channel sizes to 32 bits [(CH1WEX*16)+CH1WID+8]
    uint32_t txc_a = 0;
    
    txc_a |= (8 << PCM_CH2WID_OFFSET);
    txc_a |= (33 << PCM_CH2POS_OFFSET); // Double check -- DAC expects one clock late?
    txc_a |= (1 << PCM_CH2EN_OFFSET); // Enabling is good
    txc_a |= (1 << PCM_CH2WEX_OFFSET);

    txc_a |= (8 << PCM_CH1WID_OFFSET);
    txc_a |= (1 << PCM_CH1POS_OFFSET); // Double check -- DAC expects one clock late?
    txc_a |= (1 << PCM_CH1EN_OFFSET); // Enabling is good
    txc_a |= (1 << PCM_CH1WEX_OFFSET);

    PUT32(PCM_MODE_A, mode_a);
    PUT32(PCM_TXC_A, txc_a);

    pcm_configure_clock();
}

static uint32_t pcm_get_cs_a() {
    return GET32(PCM_CS_A);
}

static bool pcm_get_sync() {
    uint32_t cs_a = GET32(PCM_CS_A);
    return (cs_a >> PCM_CS_A_SYNC_OFFSET) & 1;
}

// Reads the sync bit, inverts it, writes the inverted value and returns it
static bool pcm_flip_sync() {
    uint32_t cs_a = GET32(PCM_CS_A);
    bool sync_inv = !((cs_a >> PCM_CS_A_SYNC_OFFSET) & 1);

    cs_a &= ~(1 << PCM_CS_A_SYNC_OFFSET);
    cs_a |= (sync_inv << PCM_CS_A_SYNC_OFFSET);

    PUT32(PCM_CS_A, cs_a);

    return sync_inv;
}

// Reads the sync bit, write the inverse, and then wait for the inverse to be read
static void pcm_sync() {
    bool sync_inv = pcm_flip_sync();

    while(pcm_get_sync() != sync_inv) {}
}

static void pcm_start() {
    pcm_init();

    uint32_t dreq_a = 0;
    dreq_a |= (0x20 << PCM_DREQ_RX_OFFSET);
    dreq_a |= (32 << PCM_DREQ_TX_OFFSET);
    dreq_a |= (0x30 << PCM_DREQ_RX_PANIC_OFFSET);
    dreq_a |= (0x10 << PCM_DREQ_TX_PANIC_OFFSET);
    PUT32(PCM_DREQ_A, dreq_a);

    PUT32(PCM_CS_A, (1 << PCM_CS_A_EN_OFFSET) | (1 << PCM_CS_A_STBY_OFFSET));
    pcm_sync();
    pcm_sync();

    uint32_t cs_a = GET32(PCM_CS_A);
    cs_a |= (1 << PCM_CS_A_TXCLR_OFFSET);
    cs_a |= (1 << PCM_CS_A_RXCLR_OFFSET);
    cs_a |= (1 << PCM_CS_A_TXERR_OFFSET);
    cs_a |= (1 << PCM_CS_A_RXERR_OFFSET);
    cs_a |= (1 << PCM_CS_A_TXSYNC_OFFSET);
    PUT32(PCM_CS_A, cs_a);
    pcm_sync();

    PUT32(PCM_CS_A, GET32(PCM_CS_A) | (1 << PCM_CS_A_DMAEN_OFFSET));
}

static void pcm_tx_on() {
    uint32_t cs_a = GET32(PCM_CS_A);
    cs_a |= (1 << PCM_CS_A_TXON_OFFSET);
    cs_a |= (1 << PCM_CS_A_TXERR_OFFSET);
    PUT32(PCM_CS_A, cs_a);
}

static void pcm_write(uint32_t sample) {
    while(!(GET32(PCM_CS_A) & (1 << PCM_CS_A_TXD_OFFSET))) {}
    PUT32(PCM_FIFO_A, sample);
}