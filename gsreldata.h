#pragma once

#include "config.h"

#define SUD_SELECTOR_OFFSET                 0
#define SIGRETURN_STACK_SP_OFFSET           16
#define RIP_AFTER_SYSCALL_STACK_SP_OFFSET   32792
#define XSAVE_AREA_STACK_SP_OFFSET          36928
/* https://www.moritz.systems/blog/how-debuggers-work-getting-and-setting-x86-registers-part-2/ */
#define XSAVE_EAX                           0b111   /* saves the x87 state, and XMM & YMM vector registers */
#define XSAVE_SIZE                          768     /* aligned to 64-byte boundary, so fine */

#ifndef __ASSEMBLER__

class SignalHandlers;

struct alignas(0x1000) GSRelativeData {
    volatile char sud_selector = 0xFF;
    SignalHandlers* signal_handlers = nullptr;
    struct {
        volatile long long* current = base;
        volatile long long base[0x1000];
    } sigreturn_stack;
    struct { // stack of `rip_after_syscall`s for use during vfork handling
        volatile char* current = base;
        volatile char base[0x1000];
    } rip_after_syscall_stack;
    struct { // xsave area stack grows up
        volatile char* current = base;
        volatile char __attribute__((aligned(64))) base[XSAVE_SIZE * 6]; // 6 nesting levels ought to be fine
    } xsave_area_stack;
};

// GS-relative accessor
inline const auto gsreldata = (__seg_gs GSRelativeData*) 0x0;

// necessary to keep asm files in sync
static_assert(__builtin_offsetof(GSRelativeData, sud_selector) == SUD_SELECTOR_OFFSET);
static_assert(__builtin_offsetof(GSRelativeData, sigreturn_stack.current) == SIGRETURN_STACK_SP_OFFSET);
static_assert(__builtin_offsetof(GSRelativeData, rip_after_syscall_stack.current) == RIP_AFTER_SYSCALL_STACK_SP_OFFSET);
static_assert(__builtin_offsetof(GSRelativeData, xsave_area_stack.current) == XSAVE_AREA_STACK_SP_OFFSET);

GSRelativeData* map_gsrel_region();
SignalHandlers* map_signal_handlers();
void enable_sud();
extern "C" void setup_new_thread(unsigned long long clone_flags);
void teardown_thread_metadata();
extern "C" void setup_restore_selector_trampoline(void* ucontextv);

#endif
