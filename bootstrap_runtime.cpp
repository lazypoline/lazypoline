#include "lazypoline.h"

#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

#define DLSYM(handle, funcname) ({                                                                  \
    dlerror(); /* clear dlsym errors  */                                                            \
    decltype(funcname)* _func = reinterpret_cast<decltype(funcname)*>(dlsym(handle, #funcname));    \
    if (const char* error = dlerror()) {                                                            \
        fprintf(stderr, "dlsym: %s\n", error);                                                      \
        exit(-1);                                                                                   \
    }                                                                                               \
    _func;                                                                                          \
})

[[gnu::constructor(0)]]
void load_runtime() {
    auto library_path = getenv("LIBLAZYPOLINE");
    if (!library_path) {
        fprintf(stderr, "'LIBLAZYPOLINE' not specified: Please set the 'LIBLAZYPOLINE' env var to the path of the lazypoline runtime library\n");
        exit(-1);
    }

    dlerror();
    auto handle = dlmopen(LM_ID_NEWLM, library_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        assert(!handle);
        fprintf(stderr, "Failed to open %s\n", dlerror());
        exit(-1);
    }

    auto init_lazypoline_fn = DLSYM(handle, init_lazypoline);
    // hand off control to lazypoline runtime further setup
    init_lazypoline_fn();
}
