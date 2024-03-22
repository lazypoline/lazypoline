#pragma once

// don't include too much here
#include <stdint.h>

extern "C" void init_lazypoline();

// two options:
//  (1) emulates the system call and returns the return value
//  (2) asks for the syscall to be emulated later on by setting *should_emulate = true. 
//      In this case, the return value should be the syscall number to emulate later on
long syscall_emulate(int64_t syscall_no, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, uint64_t* const should_emulate);
