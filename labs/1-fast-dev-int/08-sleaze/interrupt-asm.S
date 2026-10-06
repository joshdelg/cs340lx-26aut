#include "rpi-asm.h"

@ just use the default libpi interrupt stack address.
#define INT_STACK INT_STACK_ADDR

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
    @ former interrupt trampoline
    @ sub lr, lr, #4
    mov sp, #150994944
    push    {r2, r3}
    mov r3, #1
    mcr 15, 0, r3, cr13, cr0, 3
    mrc 15, 0, r3, cr13, cr0, 2
    mov r2, #134217728
    str r2, [r3]
    pop {r2, r3}
    @ movs    pc, lr
    subs pc, lr, #4

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
