#pragma once

#include "config.h"
#include <sysdeps/kernel_sigaction.h>
#include "rigtorp_spinlock.h"

#include <array>

class SignalHandlers {
    struct {
        kernel_sigaction dfl_handler;
        std::array<kernel_sigaction, NSIG> app_handlers;
    } members;
    mutable spinlock mut; // mutable for readlocks

    decltype(members.dfl_handler)& dfl_handler() { return members.dfl_handler; }
    decltype(members.app_handlers)& app_handlers() { return members.app_handlers; }

    struct kernel_sigaction get_app_handler(int signo);
    struct kernel_sigaction set_app_handler(int signo, const struct kernel_sigaction &newact);

    enum SIGDISP_TYPE { IGN, TERM, CORE, STOP, CONT };
    static SIGDISP_TYPE get_default_behavior(int signo);
    static bool is_terminating_sig(int signo) {
        auto beh = get_default_behavior(signo);
        return beh == TERM || beh == CORE; 
    }

public:
    long long handle_app_sigaction(int signo, const struct kernel_sigaction* newact, struct kernel_sigaction* oldact);
    void invoke_app_specific_handler(int sig, siginfo_t *info, void *ucontextv);

    SignalHandlers();
    SignalHandlers(const SignalHandlers& other);
};

