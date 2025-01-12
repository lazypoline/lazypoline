#include "gsreldata.h"
#include <linux/unistd.h>

.macro xsave_vector_regs_to_gsrel
#if SAVE_VECTOR_REGS
    pushq %rdx
    pushq %rax
    pushq %rsi
    xorl %edx, %edx
    movl $XSAVE_EAX, %eax
    movq %gs:XSAVE_AREA_STACK_SP_OFFSET, %rsi
    xsave (%rsi)
    addq $XSAVE_SIZE, %rsi
    movq %rsi, %gs:XSAVE_AREA_STACK_SP_OFFSET
    popq %rsi
    popq %rax
    popq %rdx
#endif
.endmacro

.macro xrstor_vector_regs_from_gsrel
#if SAVE_VECTOR_REGS
    pushq %rdx
    pushq %rax
    pushq %rsi
    xorl %edx, %edx
    movl $XSAVE_EAX, %eax
    movq %gs:XSAVE_AREA_STACK_SP_OFFSET, %rsi
    subq $XSAVE_SIZE, %rsi
    xrstor (%rsi)
    movq %rsi, %gs:XSAVE_AREA_STACK_SP_OFFSET
    popq %rsi
    popq %rax
    popq %rdx
#endif
.endmacro

.macro setup_c_stack
    pushq %rbp
    movq %rsp, %rbp

    /*
    * NOTE: for xmm register operations such as movaps
    * stack is expected to be aligned to a 16 byte boundary.
    */

    andq $-16, %rsp /* 16 byte stack alignment */

    /* this sequence of pushes until the call should maintain 16-byte stack alignment */
    pushq %r11
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %r10
.endmacro

.macro teardown_c_stack
    popq %r10
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r11

    movq %rbp, %rsp
    popq %rbp
.endmacro

.macro exit_interposer
    /* block SUD */
    movb $1, %gs:SUD_SELECTOR_OFFSET

    /* rip_after_syscall should be at top of stack here */
.endmacro

.globl asm_syscall_hook
asm_syscall_hook:
    /* pop the saved rax from when we did the trampoline jump */
    popq %rax

    /* at this point, the register & stack state is _exactly_ as at the 
        syscall invocation site, except that the rip_after_syscall is 
        pushed to the top of our stack */

    /* unblock SUD */
    movb $0, %gs:SUD_SELECTOR_OFFSET

    pushq %r12  /* we use it to check whether we should emulate the syscall or not */

    xsave_vector_regs_to_gsrel
    setup_c_stack

    /* arguments for zpoline_syscall_handler */
    movq %r10, %rcx
    xorq %r12, %r12
    pushq %r12 /* make room for `should_emulate` (false by default) */
    leaq 0x0(%rsp), %r12 /* &should_emulate -> r12 */
    pushq %r12  /* &should_emulate as last arg of zpoline_syscall_handler */
    pushq 8(%rbp) /* address of instruction after rewritten syscall */
    pushq %rax

    /* FIXME: assuming the C calling conv for `syscall` here  */
    /* in reality, we should somehow preserve _everything_ */

    /* up to here, stack has to be 16 byte aligned */
    /* the arguments are set up like so:
     zpoline_syscall_handler(rdi, rsi, rdx, r10, r8, r9, rax, ret_addr, &should_emulate) */

    callq zpoline_syscall_handler
    /* rax now contains the syscall return value */
    /* except for some special syscalls, which we handle later */

    /* we optimized for the common case, i.e., that SUD is blocked here */

    addq $24, %rsp /* discard the pushed rax, rip_after_syscall and &should_emulate */
    popq %r12 /* should_emulate -> r12 */
    
    teardown_c_stack
    xrstor_vector_regs_from_gsrel

    /* at this point, "all" of our handling mingling has been undone
        (minus some post-rsp stack changes)
       Most of the register state is the same as before (minus clobbered regs & rax & r12) 
       The stack is the same as during syscall invocation, plus the rip_after_syscall and r12 */

    /* now, we check whether we have to emulate some special system calls */
    test %r12, %r12 /* if !should_emulate: do nothing */
    popq %r12 /* restore r12 (callee-saved) */
    jz .do_nothing

    /* SUD is still unblocked here */

    /* when emulating, rax will contain the syscall to emulate */
    /* now check whether we have to sigreturn */
    /* if so, we have to re-enable the selector after the sigreturn */
    cmpq $__NR_rt_sigreturn, %rax
    je .do_rt_sigreturn

    /* check whether we have to clone */
    /* if so, do so & setup GS and SUD in child */
    cmpq $__NR_clone, %rax
    je .do_clone_thread_or_clone_vfork

    /* if neither of the above, something's wrong */
    ud2
    int3

.do_nothing:
    exit_interposer
    ret

.do_rt_sigreturn:
    /* all the syscall arguments in rdi etc should still be in the original state */
    /* but I don't think they even matter for sigreturn */

    addq $8, %rsp /* discard the pushed return address, ~ the ret in the normal path */
    /* `wrap_signal_handler` will have set REG_RIP to `restore_selector_trampoline` */
    /* and the original RIP to return to is pushed to the stack of the original program */
    syscall 
    ud2
    int3

.do_clone_thread_or_clone_vfork:
    /* push the right return address to the child's stack as well */
    pushq %r11
    movq 0x8(%rsp), %r11 /* rip_after_syscall -> clobbered reg */
    subq $8, %rsi /* make space on the child stack */
    movq %r11, 0x0(%rsi) /* rip_after_syscall -> top of child stack */
    popq %r11

    /* all args are still set up as original */
    syscall
    testq %rax, %rax
    jz .new_thread
    /* parent here: either done, or error */
    exit_interposer
    ret

.new_thread:
    /* child running on a completely new stack */
    /* we want to execute some C code here for SUD enablement
        and GS setup */
    /* still, TLS etc are not (necessarily) set up yet -> no libc code 
      -> we only call code here that is compiled with -nostdlib */
    /* save the return code */
    pushq %rax
    setup_c_stack
    /* clone_flags should still be in %rdi: perfect */
    callq setup_new_thread
    /* restore registers */
    teardown_c_stack
    /* block SUD in the child */
    exit_interposer
    /* cleanup and return to syscall site */
    popq %rax
    ret
