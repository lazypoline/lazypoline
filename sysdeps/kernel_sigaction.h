#pragma once

#include <sys/signal.h>

// from https://elixir.bootlin.com/glibc/glibc-2.31/source/sysdeps/unix/sysv/linux/x86_64/sigaction.c
#define SA_RESTORER 0x04000000

// rest is from https://elixir.bootlin.com/glibc/glibc-2.31/source/sysdeps/unix/sysv/linux/kernel_sigaction.h
#ifdef SA_RESTORER
# define HAS_SA_RESTORER 1
#endif

/* This is the sigaction structure from the Linux 3.2 kernel.  */
struct kernel_sigaction
{
  __sighandler_t k_sa_handler;
  unsigned long sa_flags;
#ifdef HAS_SA_RESTORER
  void (*sa_restorer) (void);
#endif
  /* glibc sigset is larger than kernel expected one, however sigaction
     passes the kernel expected size on rt_sigaction syscall.  */
  sigset_t sa_mask;
};

#ifndef SET_SA_RESTORER
# define SET_SA_RESTORER(kact, act)
#endif
#ifndef RESET_SA_RESTORER
# define RESET_SA_RESTORER(act, kact)
#endif

