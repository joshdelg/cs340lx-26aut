// [NOTE: this code should remind you alot about 1-fast-dev-int.
// its good practice to speed run again.]
//
// Simple code to measure the time it takes to record timed
// reads.
//
// you can use these to make a digital scope, or make bit banged
// digital protocols.  dual of observing for measurement and 
// observing for information.
//
// The file "interrupt-asm.S" has the support assembly code.
//
// You should put a loopback jumper between <in_pin> and 
// <out_pin> (definitions below).
//
// Lab: make this code much faster / accurate.
// For some reason my demand.h doesn't have the NDEBUG macro
#define assert(x) do { } while(0)

#include "rpi.h"
#include "timer-interrupt.h"
#include "cycle-count.h"
#include "vector-base.h"
#include "gpio-raw.h"
#include "asm-helpers.h"

cp_asm_raw(cp15_scratch3, p15, 0, c13, c0, 4)
cp_asm_raw(cp15_scratch2, p15, 0, c13, c0, 3)
cp_asm_raw(cp15_scratch1, p15, 0, c13, c0, 2)

void interrupt_setup_stack(void);

// raw timed reads: gives the cycle count (absolute) and the raw
// level.  post process this into a timed log(?)
typedef struct { 
    uint32_t lev;   // raw lev
    uint32_t cyc;   // raw cycles
} timed_read_t;

static inline timed_read_t * tr_get(void)
    { return (void*)cp15_scratch1_get(); }
static inline void tr_set(timed_read_t *t)
    { cp15_scratch1_set_raw((uint32_t)t); }

static inline volatile uint32_t *event0_get(void)
    { return (void*)cp15_scratch2_get(); }
static inline void event0_set(uint32_t event0)
    { cp15_scratch2_set_raw(event0); }

static inline volatile uint32_t *level0_get(void)
    { return (void*)cp15_scratch3_get(); }
static inline void level0_set(uint32_t level0)
    { cp15_scratch3_set_raw(level0); }

// not sure if you should check the end.
// probably should just do a watchpoint.
enum { MAX_READS = 128 };
static timed_read_t t_reads[MAX_READS], *t_end = &t_reads[MAX_READS];
static timed_read_t * volatile tr = &t_reads[0];

static inline timed_read_t * volatile read_cur(void) {
    return tr;
}

// Can change these pins to whatever you want.  
// 
// NOTE: some pin numbers let the compiler generate faster 
// code b/c it can load derived constants in fewer instructions
// [useful side quest: write some code to check this claim!]
enum { out_pin = 26, in_pin = 27 };

static void int_init(void) {
    tr = &t_reads[0];

    level0_set(GPIO_LEV0);
    event0_set(GPIO_EVENT_DETECT0);
}

static inline void dmb_raw(void) {
    uint32_t r0 = 0;
    asm volatile("mcr p15, 0, %0, c7, c10, 5" :: "r"(r0));
}

// a bit fancier than the device interrupt since we 
// are recording a log.
__attribute__((interrupt("FIQ"), aligned(32))) void fiq_handler(void) {
    uint32_t cycle = cycle_cnt_read();

    // we don't know what the user code was doing
    dmb_raw();
    // uint32_t v = gpio_read_raw(in_pin);
    uint32_t lev = *level0_get();

    timed_read_t *tr = tr_get();
    *tr =  (timed_read_t) { .cyc = cycle, .lev = lev };
    tr_set(tr + 1);

    assert(tr <= t_end);

    // gpio and event clear may need device barrier?
    dmb_raw();

    // gpio_event_clear_raw(in_pin);
    *event0_get() = 1 << in_pin;

    // we don't know what the user code was doing
    dmb_raw();
}

// driver that triggers and measures the interrupts
// caused by writing to GPIO <pin>.
void test_cost(unsigned pin) { 
    int_init();
    // initial state.
    assert(gpio_read_raw(in_pin) == 0);

    float sum = 0;
    uint32_t c,e;
    enum { N = 20 };

    for(int i = 0; i < N/2; i++) {
        // measure the cost of a rising edge interrupt.
        // by reading cycle counter and spinning until the
        // rising edge count increases (i.e., an interrupt
        // occured).
        // timed_read_t* r = read_cur();
        timed_read_t* r = tr_get();
        asm volatile(".align 5");
        c = cycle_cnt_read();
        // gpio_set_on(pin);
        gpio_set_on_raw(pin);
        // while(r == read_cur())
        while(tr_get() == r)
            ;
        e = cycle_cnt_read();
        // compute what the level was
        let lvl = (r->lev >> in_pin) &1;

        output("%d: rising  [lev=%d]\t= %d total cycles [%d until int ran]\n", 
            i*2, 
            lvl,
            e-c,
            r->cyc -c);
        sum += e-c;
        if(lvl != 1)
            panic("LOG error: level not 1 on a rising interrupt\n");

        // measure the cost of a falling edge interrupt.
        // by reading cycle counter and spinning until the
        // falling edge count increases (i.e., an interrupt
        // occured).
        // r = read_cur();
        r = tr_get();
        asm volatile(".align 5");
        c = cycle_cnt_read();
        // gpio_set_off(pin);
        gpio_set_off_raw(pin);
        // while(r == read_cur())
        while(tr_get() == r)
            ;
        e = cycle_cnt_read();

        // compute what the level was
        lvl = (r->lev >> in_pin) &1;

        output("%d: falling [lev=%d]\t= %d total cycles [%d until int ran]\n", 
            i*2+1, lvl, e-c, r->cyc - c);
        sum += e-c;
        if(lvl != 0)
            panic("LOG error: level not 0 on a falling interrupt\n");
    }
    output("ave cost = %f\n", sum / 20);
}

// all this is the same as 1-fast-dev-int.
void notmain() {
    //*****************************************************
    // 1. setup pins and check that loopback works.
    // gpio_set_output(out_pin);
    // gpio_set_input(in_pin);

    // Only set the pin to output, we're going to have it watch its own edges
    gpio_set_output(in_pin);

    // make sure there is a jumper b/n <in_pin> and <out_pin>
    // gpio_write(out_pin, 1);
    // if(gpio_read(in_pin) != 1)
    //     panic("connect jumper from pin %d to pin %d\n", 
    //                                 in_pin, out_pin);
    // gpio_write(out_pin, 0);
    // if(gpio_read(in_pin) != 0)
    //     panic("connect jumper from pin %d to pin %d\n", 
    //                                 in_pin, out_pin);

    //*****************************************************
    // 2. setup interrupts in our standard way.
    extern uint32_t default_vec_ints[];

    // setup interrupts.  you've seen this code
    // before.  (we're assuming ints are off.)
    dev_barrier();
    PUT32(IRQ_Disable_1, 0xffffffff);
    PUT32(IRQ_Disable_2, 0xffffffff);
    dev_barrier();
    vector_base_set(default_vec_ints);

    // setup interrupts on both rising and falling edges.
    // @joshdelg -- These should work even though its an output pin
    gpio_fiq_rising_edge(in_pin);
    gpio_fiq_falling_edge(in_pin);

    // the above sample twice triggering (for a stable
    // signal) --- in theory these should be faster.
    // gpio_int_async_rising_edge(in_pin);
    // gpio_int_async_falling_edge(in_pin);

    // clear any existent GPIO event so that we don't 
    // get a delayed interrupt.
    gpio_event_clear(in_pin);

    output("About to go live.\n");
    interrupt_setup_stack();
    enable_interrupts();
    // now we are live!

    //*****************************************************
    // 3. run the test.

    // leave this off initially so its easier to see the effect
    // of speed improvements.
    // caches_enable();

    // test_cost(out_pin);
    test_cost(in_pin);
    output("Done!\n");
    return;
}
