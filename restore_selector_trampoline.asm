#include "gsreldata.h"

/* This is the landingpad for sigreturns from user-supplied signal handlers */
/* We just have to restore the selector value, and then jump back to where 
    the sigreturn should have actually went to (the "old RIP")
    We have to do so transparantly, without clobbering any registers */
/* `wrap_signal_handler` will have pushed the old RIP to the top of the stack here */
/* the privilege level to restore to will be on the sigreturn stack */
.globl restore_selector_trampoline
restore_selector_trampoline:
    /* we've intercepted all signal-handler syscalls */
    /* restore the selector to the value it had during the delivery of the signal */
    pushq %rax
    pushq %rdx
    pushq %rcx

    /* we always enter this trampoline with unblocked SUD */

    /* pop & apply saved SUD permissions from the sigreturn stack */
    movq %gs:SIGRETURN_STACK_SP_OFFSET, %rax
    decq %rax
    movb 0(%rax), %dl /* get privilege level into dl */
    movq %rax, %gs:SIGRETURN_STACK_SP_OFFSET /* update sigreturn stack pointer */
    movb %dl, %gs:SUD_SELECTOR_OFFSET

.return_to_app:
    popq %rcx
    popq %rdx
    popq %rax
    ret /* old RIP sits at top of stack */

.section .note.GNU-stack