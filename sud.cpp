#include "sud.h"

#include "lazypoline.h"
#include "zpoline.h"
#include "gsreldata.h"
#include "signal_handlers.h"

#include <immintrin.h>
#include <stddef.h>
#include <sys/auxv.h>

UnblockScope::UnblockScope () {
    oldselector = get_privilege_level();
    set_privilege_level(SYSCALL_DISPATCH_FILTER_ALLOW);
}

UnblockScope::~UnblockScope () {
    assert(get_privilege_level() == SYSCALL_DISPATCH_FILTER_ALLOW);
    set_privilege_level(oldselector);
}

BlockScope::BlockScope () {
    oldselector = get_privilege_level();
    set_privilege_level(SYSCALL_DISPATCH_FILTER_BLOCK);
}
BlockScope::~BlockScope () {
    assert(get_privilege_level() == SYSCALL_DISPATCH_FILTER_BLOCK);
    set_privilege_level(oldselector);
}

struct vdso_location {
    const uint8_t* const start;
    const size_t len;

    static vdso_location& get() {
        // this length might be too small, depending on kernel version
        static vdso_location _vdso{(uint8_t*) getauxval(AT_SYSINFO_EHDR), 0x1000};
        return _vdso;
    }

    bool contains(uint8_t* addr) {
        if (addr >= start && addr < start + len)
            return true;
        return false;
    }

private:
    vdso_location(uint8_t* start, size_t len) : 
        start{start}, len{len}
    {
        ASSERT_ELSE_PERROR(start);
        assert(__builtin_is_aligned(start, 0x1000));
    }
};

// This handler should _only_ return after the REG_RIP has been repointed to the asm_syscall_hook
// otherwise, the selector will not be properly reset to BLOCK, and the syscall will not be properly emulated
static void handle_sigsys(int sig, siginfo_t *info, void *ucontextv) {
    set_privilege_level(SYSCALL_DISPATCH_FILTER_ALLOW);

    // see the sigaction handling for more info
    assert(info->si_code == SYS_USER_DISPATCH && "SUD does not support safely running non-SUD SIGSYS handlers!");
    assert(sig == SIGSYS);
	assert(info->si_signo == SIGSYS);
	assert(info->si_errno == 0);

#if REWRITE_TO_ZPOLINE
    auto& vdso = vdso_location::get();
    auto syscall_addr = &((uint16_t*)info->si_call_addr)[-1];
    if (!vdso.contains((uint8_t*) syscall_addr)) 
        rewrite_syscall_inst(syscall_addr);
#endif
    
    // emulate the system call by invoking the asm_syscall_hook entrypoint (single entry point for syscall emulation)
    // this has the added advantage that we're not in a signal handler when handling certain system calls
    //  that has been an issue when receiving signals while in blocking system calls (e.g. waiting on socket)
    // we have to set up the stack "as if" we were coming from the rewritten `callq *%(rax)`, i.e. push our 
    // return address to the stack
    // FIXME: I think, for safety, it's better to run this handler on a different stack. Otherwise our stack pushes
    // might start overwriting our sighandler local vars
    const auto uctxt = (ucontext_t*) ucontextv;
	const auto gregs = uctxt->uc_mcontext.gregs;
	assert(gregs[REG_RAX] == info->si_syscall);
    // push RIP and RAX: this clobbers memory beyond the end of the stack, which isnt necessary for SUD
    // but it is necessary for zpoline
    gregs[REG_RSP] -= 2 * sizeof(uint64_t);
    auto stack_bottom = ((long long*)gregs[REG_RSP]);
    stack_bottom[1] = gregs[REG_RIP];
    stack_bottom[0] = gregs[REG_RAX];
    // we keep RAX as the syscall no here, so we can potentially detect in
    // `asm_syscall_hook` whether we came from SUD or not
    // "jmpq asm_syscall_hook". If we came from a rewritten syscall, 
    // `rax` would be the `asm_syscall_hook` address (zpoline.cpp)
    // I suppose we could also figure that out from the `sud_selector` value
    gregs[REG_RIP] = (long long) asm_syscall_hook;

    assert(gregs[REG_RAX] == info->si_syscall);
    // sigreturn to the asm_syscall_hook!
}

void init_sud() {
    map_gsrel_region();
    assert(gsreldata == 0);
    static_assert(offsetof(GSRelativeData, sud_selector) == 0); // asm code depends on this
    set_privilege_level(SYSCALL_DISPATCH_FILTER_ALLOW);

    gsreldata->signal_handlers = map_signal_handlers();
    new (gsreldata->signal_handlers) SignalHandlers();

    struct sigaction act = {};
	act.sa_sigaction = handle_sigsys;
	act.sa_flags = SA_SIGINFO;
    ASSERT_ELSE_PERROR(sigemptyset(&act.sa_mask) == 0);
	ASSERT_ELSE_PERROR(sigaction(SIGSYS, &act, NULL) == 0);	
}
