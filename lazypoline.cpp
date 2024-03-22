#include "lazypoline.h"

#include "sud.h"
#include "zpoline.h"
#include "signals.h"
#include <sysdeps/kernel_sigaction.h>
#include "gsreldata.h"

#include <syscall.h>
#include <sys/signal.h>
#include <sched.h>
#include <unistd.h>
#include <immintrin.h>

#ifndef CLONE_CLEAR_SIGHAND
#define CLONE_CLEAR_SIGHAND 0x100000000ULL
#endif

void init_lazypoline() {
    fprintf(stderr, "Initializing lazypoline!\n");

    init_sud();
#if REWRITE_TO_ZPOLINE
    init_zpoline();
#endif

	enable_sud();
    set_privilege_level(SYSCALL_DISPATCH_FILTER_BLOCK);
}

/* you can't modify the argument registers here, sorry */
[[maybe_unused]]
void precall_rt_sigreturn(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {
    // note: the stack isn't set up the right way to actually figure out the ucontext from here
    // if anyone needs that, we can supply the necessary information as arguments
}

[[maybe_unused]]
void precall_rt_sigaction(int64_t&, int64_t&, int64_t&, int64_t&, int64_t&, int64_t&) {

}

[[maybe_unused]]
void postcall_rt_sigaction(int64_t&, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {

}

/* currently just falls back to clone, will never succeed */
[[maybe_unused]]
void precall_clone3() { 
    
}

// can't modify the args here because it might get handled later on
[[maybe_unused]]
void precall_clone(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {

}

// called in both parent and child (if clone was successful)
[[maybe_unused]]
void postcall_clone_process(int64_t&, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {

}

[[maybe_unused]]
void precall_fork(int64_t&, int64_t&, int64_t&, int64_t&, int64_t&, int64_t&) {

}

// called in both parent and child (if fork was successful)
[[maybe_unused]]
void postcall_fork(int64_t&, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {

}

[[maybe_unused]]
void precall_rt_sigprocmask(int64_t&, int64_t&, int64_t&, int64_t&, int64_t&, int64_t&) {

}

[[maybe_unused]]
void postcall_rt_sigprocmask(int64_t&, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {

}

// implemented in `handle_clone_thread.asm`
extern "C" long handle_clone_thread(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6);

static void do_postfork_handling(int64_t result) {
    if (result < 0) {
        // error, no problem
    } else if (result > 0) {
        // parent, do nothing
    } else {
        // child, re-enable SUD
        enable_sud();
    }
}

long syscall_emulate(const int64_t syscall_no, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, uint64_t* const should_emulate) {
    // early return used for benchmarking the nop sled
#if RETURN_IMMEDIATELY
    return 0;
#endif

    assert(*should_emulate == false);
#if PRINT_SYSCALLS
    // common precall
    fprintf(stderr, "\e[31m[%d] syscall(%s [%ld], 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx)\e[m\n", getpid(), get_syscall_name(syscall_no), syscall_no, a1, a2, a3, a4, a5, a6);
#endif

    assert(syscall_no != __NR_vfork);

    if (syscall_no == __NR_clone3) { // fallback to clone
        precall_clone3();
        return -ENOSYS;
    }

    if (syscall_no == __NR_fork) {
        precall_fork(a1, a2, a3, a4, a5, a6);
        auto result = inline_syscall6(syscall_no, a1, a2, a3, a4, a5, a6);
        do_postfork_handling(result);
        postcall_fork(result, a1, a2, a3, a4, a5, a6);
        return result;
    }

    if (syscall_no == __NR_clone) {
        auto& flags = a1;
        auto& stack = a2;
        // auto& parent_tid = a3;
        // auto& child_tid = a4;
        // auto& tls = a5;

        precall_clone(a1, a2, a3, a4, a5, a6);

        if (flags & CLONE_THREAD) {
            // thread-like handling
            assert(flags & CLONE_VM);
            assert(!(flags & CLONE_VFORK)); // weird
            assert(!(flags & CLONE_CLEAR_SIGHAND)); // dont clear SIGSYS handler
            assert(stack);

            // clone will be emulated later on
            *should_emulate = true;
            return __NR_clone;
        } else if (flags & CLONE_VFORK) {
            // vfork-like handling
            // parent won't do anything until child calls exec/quits
            // should be handled just like CLONE_THREAD: it's as if the parent just happens to not get scheduled
            assert(stack); // we push to the child stack from the parent, later
            assert(flags & CLONE_VM);
            assert(!(flags & CLONE_THREAD));
            assert(!(flags & CLONE_CLEAR_SIGHAND)); // dont clear SIGSYS handler

            *should_emulate = true;
            return __NR_clone;
        } else {
            // fork-like handling
            assert((void*) stack == NULL);
            assert(!(flags & CLONE_CLEAR_SIGHAND)); // dont clear SIGSYS handler
            assert(!(flags & CLONE_THREAD) && !(flags & CLONE_DETACHED)); // don't support threading
            assert(!(flags & CLONE_SIGHAND)); // our signal handler metadata assumes separate signal handler tables per process
            assert(!(flags & CLONE_VFORK)); // don't think our fork handling can deal with this
            assert(!(flags & CLONE_VM)); // don't share memory between parent and child

            auto result = inline_syscall6(syscall_no, a1, a2, a3, a4, a5, a6);
            do_postfork_handling(result);
            postcall_clone_process(result, a1, a2, a3, a4, a5, a6);
            return result;
        }

        assert(!"Unreachable");
    }

    if (syscall_no == __NR_rt_sigprocmask) {
        int how = a1;
        auto set = (sigset_t*) a2;
        auto oldset [[maybe_unused]] = (sigset_t*) a3;
        auto sigsetsize = a4;
        assert(sigsetsize <=  (long long) sizeof(sigset_t));

        // sanity checking the sigsetsize parameter
        assert(sigsetsize == SIGSETSIZE);

        char modifiable_mask[SIGSETSIZE] = { 0 };
        if (set && (how == SIG_BLOCK || how == SIG_SETMASK)) {
            memcpy(modifiable_mask, set, sigsetsize);
            ASSERT_ELSE_PERROR(sigdelset((sigset_t*) modifiable_mask, SIGSYS) == 0);
            a2 = (int64_t) &modifiable_mask[0];
        }

        precall_rt_sigprocmask(a1, a2, a3, a4, a5, a6);
        auto result = inline_syscall6(syscall_no, a1, a2, a3, a4, a5, a6);
        postcall_rt_sigprocmask(result, a1, a2, a3, a4, a5, a6);
        return result;
    }

    if (syscall_no == __NR_rt_sigaction) {
        precall_rt_sigaction(a1, a2, a3, a4, a5, a6);
        int64_t result = gsreldata->signal_handlers->handle_app_sigaction(a1, (const struct kernel_sigaction*) a2, (struct kernel_sigaction*) a3);
        postcall_rt_sigaction(result, a1, a2, a3, a4, a5, a6);
        return result;
    }

    if (syscall_no == __NR_rt_sigreturn) {
        precall_rt_sigreturn(a1, a2, a3, a4, a5, a6);
        // sigreturn will be emulated later on, in `asm_syscall_hook`/`do_rt_sigreturn`
        *should_emulate = true;
        return __NR_rt_sigreturn;
    }

    if (syscall_no == __NR_exit) {
        // kills the current thread, we'd best unmap some things
        teardown_thread_metadata();
    }
    
    return inline_syscall6(syscall_no, a1, a2, a3, a4, a5, a6);
}

