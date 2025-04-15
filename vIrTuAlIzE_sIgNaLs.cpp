#include "signals.h"

#include "util.h"
#include "sud.h"
#include "lazypoline.h"
#include "gsreldata.h"

#include <syscall.h>
#include <sys/signal.h>
#include <immintrin.h>

#ifndef SA_UNSUPPORTED
#define SA_UNSUPPORTED	0x00000400
#endif

#ifndef SA_EXPOSE_TAGBITS
#define SA_EXPOSE_TAGBITS 0x00000800
#endif
// implemented in restore_selector_trampoline.asm
extern "C" void restore_selector_trampoline();

void wrap_signal_handler(int signo, siginfo_t* info, void* ucontextv) {
    char selector_on_signal_entry = get_privilege_level();

    // the signal handler might get called from within trusted code, or in untrusted code
    // either way, we have to grant syscall access here until we invoke the app-specific handler
    gsreldata->sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;

    const auto uctxt = (ucontext_t*) ucontextv;
	const auto gregs = uctxt->uc_mcontext.gregs;
    auto rsp = gregs[REG_RSP];
    gsreldata->signal_handlers->invoke_app_specific_handler(signo, info, ucontextv);

    // check whether the app-specific handler modified any of the registers we care about
    assert(rsp == gregs[REG_RSP]);

    // the signal handler is going to return here, and we will intercept the sigreturn (selector = off)
    // after we intercept the sigreturn, we will have to emulate it without interception, then re-enable the interception

    // handle nested signal delivery through a stack of selector_on_entry's
    gsreldata->sigreturn_stack.current[0] = selector_on_signal_entry;
    gsreldata->sigreturn_stack.current++;
    // always deprivilege here so we intercept the sigreturn, 
    // but the restore trampoline will restore the original privilege level to the selector_on_signal_entry
    set_privilege_level(SYSCALL_DISPATCH_FILTER_BLOCK);
}

SignalHandlers::SignalHandlers() {
    // fill the array with all the SIG_DFL kernel_sigactions upfront
    for (int i = 0; i < _NSIG; i++) {
        if (i == SIGKILL)
            continue;

        struct kernel_sigaction dfl_act;
        auto result = non_libc_rt_sigaction(i, NULL, &dfl_act);
        if (result)
            continue;
        set_app_handler(i, dfl_act);
        if (dfl_act.k_sa_handler == SIG_DFL)
            dfl_handler() = dfl_act;
    }
}

SignalHandlers::SignalHandlers(const SignalHandlers& other) {
    std::lock_guard guard{other.mut};
    members = other.members;
}

/* needs syscall access to set signal masks etc. */
void SignalHandlers::invoke_app_specific_handler(int sig, siginfo_t *info, void *ucontextv) {
    // fprintf(stderr, "Got signal %s [%d]\n", sys_siglist[sig], sig);

    mut.lock();
    auto app_handler = get_app_handler(sig);
    // we probably don't want to emulate SIG_DFLs
    assert(app_handler.k_sa_handler != SIG_DFL);
    assert(app_handler.k_sa_handler != SIG_IGN);

    assert(!(app_handler.sa_flags & SA_ONSTACK)); // we dont support altstacks

    if (app_handler.sa_flags & SA_RESETHAND) // reset to default disposition
		set_app_handler(sig, dfl_handler());
    mut.unlock();

    // app specific handler should have its syscalls intercepted
    BlockScope guard{}; 

    // call app handler
    ((void (*)(int, siginfo_t*, void*)) app_handler.k_sa_handler)(sig, info, ucontextv);
}

struct kernel_sigaction SignalHandlers::get_app_handler(int signo) {
    return app_handlers().at(signo);
}

struct kernel_sigaction SignalHandlers::set_app_handler(int signo, const struct kernel_sigaction &newact) {
    auto old = get_app_handler(signo);
    app_handlers().at(signo) = newact;
    return old;
}

// FIXME: should prob block altstack (haven't thought about the consequences of it, but i think it'll be weird)
long long SignalHandlers::handle_app_sigaction(int signo, const struct kernel_sigaction *newact, struct kernel_sigaction *oldact) {
    // hold lock while operating on the app_handlers
    std::lock_guard guard{mut};

    if (signo == SIGSYS) {
        // there seems to be an issue with a SUD SIGSYS being delivered while handling any other SIGSYS
        // e.g. executing any SUD-intercepted system call from any SIGSYS handler will terminate the program with SIGSYS
        // This should not be the kernel's behavior, but we also don't have to care: non-SUD SIGSYSes are generally non-recoverable anyway
        // note: non-SUD SIGSYSes _are_ nestable for some reason.
        // FIXME: right now we just ignore handler registration for SIGSYS, and always terminate the program on non-SUD SIGSYS
        if (newact) { // assert that the app doesn't ask for a bunch of info we don't provide
            assert(!(newact->sa_flags & SA_UNSUPPORTED)); // we don't support dynamically probing for flag bits
        }

        if (oldact)
            *oldact = get_app_handler(SIGSYS);
        return 0;
    }

    // if we don't change the newact, or make it ignored
    if (!newact || newact->k_sa_handler == SIG_IGN) {
        auto result = non_libc_rt_sigaction(signo, newact, oldact);
        if (result)
            return result;
        if (oldact)
            *oldact = get_app_handler(signo);
        if (newact)
            set_app_handler(signo, *newact);
        return result;
    }

    // unregister wrapper when we unregister the handler
    if (newact->k_sa_handler == SIG_DFL) {
        auto result = non_libc_rt_sigaction(signo, newact, oldact);
        if (result == 0) {
            auto old = set_app_handler(signo, *newact);
            if (oldact)
                *oldact = old;
        }
        return result;
    }

    struct kernel_sigaction newact_cpy = *newact;
    newact_cpy.sa_flags |= SA_SIGINFO;
    newact_cpy.k_sa_handler = (decltype(newact_cpy.k_sa_handler)) wrap_signal_handler;
    auto result = non_libc_rt_sigaction(signo, &newact_cpy, oldact);
    if (result)
        return result;

    auto old = set_app_handler(signo, *newact);
    if (oldact)
        *oldact = old;

    return result;
}

// defaults taken from https://man7.org/linux/man-pages/man7/signal.7.html
// deduplicated and unused sigs removed
SignalHandlers::SIGDISP_TYPE SignalHandlers::get_default_behavior(int signo) {
	switch (signo) {
		// ignored sigs
		case SIGCHLD:
		case SIGURG:
		case SIGWINCH:
			return SIGDISP_TYPE::IGN;

		// terminating sigs
		case SIGALRM:
		case SIGHUP:
		case SIGINT:
		case SIGIO:
		case SIGKILL:
		case SIGPIPE:
		case SIGPROF:
		case SIGPWR:
		case SIGSTKFLT:
		case SIGTERM:
		case SIGUSR1:
		case SIGUSR2:
		case SIGVTALRM:
			return SIGDISP_TYPE::TERM;

		// coredump sigs
		case SIGABRT:
		case SIGBUS:
		case SIGFPE:
		case SIGILL:
		case SIGQUIT:
		case SIGSEGV:
		case SIGSYS:
		case SIGTRAP:
		case SIGXCPU:
		case SIGXFSZ:
			return SIGDISP_TYPE::CORE;

		// stop sigs
		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			return SIGDISP_TYPE::STOP;

		// cont sigs
		case SIGCONT:
			return SIGDISP_TYPE::CONT;

		default:
			assert(!"Unknown signal!");
	}
}
