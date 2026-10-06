#include "rpi-asm.h"

@ just use the default libpi interrupt stack address.
#define INT_STACK INT_STACK_ADDR
#define INT_BASE_ADDR 0x2000B000
#define FIQ_REG_ADDR 0x2000B20C
#define FIQ_INT_SRC 49
#define GPIO_EVENT_CLR 0x20200040

@ register r8 holds the event clear address
#define event0      r8
@ register r9 holds the value (1<<in_pin) that 
@ we write to event clear address
#define event0_val  r9
@ register r10 holds the value 1 that we write to 
@ the scratch register to indicate an interrupt happened
#define one         r10

.globl fiq_int_setup
fiq_int_setup:
    @ set FIQ register 7th bit to 1 and lower 6 to source
    ldr r0, =FIQ_REG_ADDR
    ldr r1, [r0]
    orr r1, r1, #(1 << 7)
    bic r1, r1, #0x3F
    orr r1, r1, #FIQ_INT_SRC
    str r1, [r0]

    @ Switch to FIQ mode and set up our regs
    cps #FIQ_MODE
    prefetch_flush(r1)
    ldr event0, =GPIO_EVENT_CLR
    mov event0_val, #(1 << 27)
    mov one, #1
    cps #SUPER_MODE
    prefetch_flush(r1)

    bx lr

.align 5;
.globl fiq_vec_ints
fiq_vec_ints:
    @ see below: these jump to default libpi handlers and panic.
    b reset
    b undef
    b syscall
    b prefetch_abort
    b data_abort
    b reset
    b reset
    @ Switching these adds 140 cycles LMAO
    str event0_val, [event0]
    mcr 15, 0, sl, cr13, cr0, 3
    subs pc, lr, #4

.align 5; 
.globl default_vec_ints
default_vec_ints: 
    @ see below: these jump to default libpi handlers and panic.
    b reset
    b undef
    b syscall
    b prefetch_abort
    b data_abort
    b reset

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ these are trampolines for unexpected exceptions: they just
@ die with an error.  you don't have to modify them.

#define MK_TRAMPOLINE(name, offset, fn)         \
    name:                                       \
        sub   lr, lr, # (offset);               \
        mov sp, #INT_STACK;               \
        push  {r0-r12,lr};                \
        mov   r0, lr;                     \
        bl    fn;                         \
        pop   {r0-r12,lr};                \
        movs    pc, lr


@ call the default libpi handlers, which panic with an error 
@ message.  override if you want to do something fancier.
MK_TRAMPOLINE(reset,            4, reset_vector)
MK_TRAMPOLINE(undef,            4, undefined_instruction_vector)
MK_TRAMPOLINE(prefetch_abort,   4, prefetch_abort_vector)
MK_TRAMPOLINE(data_abort,       8, data_abort_vector)
MK_TRAMPOLINE(syscall,          4, syscall_vector)
