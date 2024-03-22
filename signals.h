#pragma once

#include "config.h"
#include "util.h"
#include <sysdeps/kernel_sigaction.h>
#include "rigtorp_spinlock.h"
#include "signal_handlers.h"

#include <assert.h>
#include <syscall.h>

#include <array>

class SetRtSigProcMaskScope {
    sigset_t ourmask = {};
    const sigset_t newmask;
public:
    SetRtSigProcMaskScope(const sigset_t& newmask) : 
        newmask{newmask}
    {
        auto result = non_libc_rt_sigprocmask(SIG_SETMASK, &newmask, &ourmask);
        if (result < 0) {
            errno = -result;
            perror("rt_sigprocmask");
        }
        assert(result == 0);
    }

    ~SetRtSigProcMaskScope() {
        sigset_t dbg_newmask = {};
		auto result = non_libc_rt_sigprocmask(SIG_SETMASK, &ourmask, &dbg_newmask);
        assert(result == 0);
        for (int i = 0; i < NSIG; i++) 
            assert(sigismember(&dbg_newmask, i) == sigismember(&newmask, i));
    }
};

struct BlockAllSignalsScope : private SetRtSigProcMaskScope {
    BlockAllSignalsScope() : 
        SetRtSigProcMaskScope{({
            sigset_t all_sigs;
            ASSERT_ELSE_PERROR(sigfillset(&all_sigs) == 0);
            ASSERT_ELSE_PERROR(sigdelset(&all_sigs, SIGKILL) == 0);
            ASSERT_ELSE_PERROR(sigdelset(&all_sigs, SIGSTOP) == 0);
            all_sigs;
        })}
    {}
};

