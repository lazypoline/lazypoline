#pragma once

#include "config.h"
#include <sys/prctl.h>

#ifndef PR_SET_SYSCALL_USER_DISPATCH
# define PR_SET_SYSCALL_USER_DISPATCH	59
# define PR_SYS_DISPATCH_OFF	0
# define PR_SYS_DISPATCH_ON	1
# define SYSCALL_DISPATCH_FILTER_ALLOW	0
# define SYSCALL_DISPATCH_FILTER_BLOCK	1
#endif

#ifndef SYS_USER_DISPATCH
# define SYS_USER_DISPATCH 2	/* syscall user dispatch triggered */
#endif

class UnblockScope {
    char oldselector;
public:
    UnblockScope ();
    ~UnblockScope ();
};

class BlockScope {
    char oldselector;
public:
    BlockScope ();
    ~BlockScope ();
};

void init_sud();

__attribute__((retain))
char get_privilege_level();
void set_privilege_level(char sud_status);
