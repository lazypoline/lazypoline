#include "gsreldata.h"
#include "nolibc_util.h"
#include "signal_handlers.h"

#include <linux/sched.h>
#include <linux/prctl.h>
#include <linux/unistd.h>
#include <linux/mman.h>

#include <immintrin.h>

#define PR_SET_SYSCALL_USER_DISPATCH	59
#define PR_SYS_DISPATCH_OFF	            0
#define PR_SYS_DISPATCH_ON	            1
#define SYSCALL_DISPATCH_FILTER_ALLOW	0
#define SYSCALL_DISPATCH_FILTER_BLOCK	1

/* Watch out with the code you call here: 
    it shouldn't do any TLS stuff, and shouldn't call glibc either
*/

#ifndef assert
#define assert(cond)        \
    do {                    \
        if (!(cond))          \
            asm ("int3");   \
    } while (0);
#endif

char get_privilege_level() {
    return gsreldata->sud_selector;
}

void set_privilege_level(char sud_status) {
    if (sud_status == SYSCALL_DISPATCH_FILTER_ALLOW) {
        gsreldata->sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;
    } else {
        assert(sud_status == SYSCALL_DISPATCH_FILTER_BLOCK);
        gsreldata->sud_selector = SYSCALL_DISPATCH_FILTER_BLOCK;
    }
}

GSRelativeData* map_gsrel_region() {
    auto mem = (void*) inline_syscall6(__NR_mmap, 0x0, sizeof(GSRelativeData), PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(mem != (void*) -1 && mem != 0);
    auto gsreldata = new ((void*)mem) GSRelativeData();
    _writegsbase_u64((long long) mem);
    return gsreldata;
}

void teardown_thread_metadata() {
    // never unmap the sigdisps since it's too hard/racy to figure out if anyone is still using them

    // ensure the kernel won't try to access the unmapped selector
    long result = inline_syscall6(__NR_prctl, PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_OFF, 0x0, 0, 0, 0);
    assert(result == 0);
    result = inline_syscall6(__NR_munmap, (void*)_readgsbase_u64(), __builtin_align_up(sizeof(GSRelativeData), 0x1000), 0, 0, 0, 0);
    assert(result == 0);
}

void enable_sud() {
    volatile char* selector_addr = ((char*) _readgsbase_u64()) + SUD_SELECTOR_OFFSET;
    long result = inline_syscall6(__NR_prctl, PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, 0x0, 0, selector_addr, 0);
	assert(result == 0);
}

SignalHandlers* map_signal_handlers() {
    auto result = inline_syscall6(__NR_mmap, 0x0, sizeof(SignalHandlers), PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(result != -1 && result != 0);
    return (SignalHandlers*) result;
}

extern "C" void setup_new_thread(unsigned long long clone_flags) {
    auto cloner_gsrel = (GSRelativeData*) _readgsbase_u64();
    GSRelativeData* gsreldata = map_gsrel_region();
    set_privilege_level(SYSCALL_DISPATCH_FILTER_ALLOW);

    if (clone_flags & CLONE_SIGHAND) {
        // share sigdisps with caller
        gsreldata->signal_handlers = cloner_gsrel->signal_handlers;
    } else {
        // duplicate em
        gsreldata->signal_handlers = map_signal_handlers();
        new ((char*)gsreldata->signal_handlers) SignalHandlers(*cloner_gsrel->signal_handlers);    
    }

    // re-enable SUD
    enable_sud();
}
