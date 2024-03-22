# this is definitely not the lowest supported version
cmake_minimum_required(VERSION 3.16)

project(lazypoline C CXX ASM)

# not strictly required, but we use some builtins
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(COMMON_COMPILE_OPTIONS -Wall -Wextra -ggdb -fPIC -fno-exceptions -march=native)
set(OPTIMIZATION -O3 -flto)
set(LAZYPOLINE_OPTIONS ${COMMON_COMPILE_OPTIONS} ${OPTIMIZATION})
set(COMMON_LINK_OPTIONS ${OPTIMIZATION})

set_property(
    SOURCE 
        restore_selector_trampoline.asm 
        asm_syscall_hook.asm 
    APPEND PROPERTY COMPILE_OPTIONS "-x" "assembler-with-cpp"
)

# object lib for code that can execute right after thread creation
# some restrictions on it; mostly shouldn't call libc code
# we just have this to avoid having to write a bunch of ASM
# FIXME: -nostdlib doesn't seem to do much though, and as a link option
# it gives undefined symbols
add_library(setup_thread STATIC setup_new_thread.cpp)
target_compile_options(setup_thread PUBLIC ${COMMON_COMPILE_OPTIONS} -nostdlib -ffreestanding -fno-exceptions -O3)
target_include_directories(setup_thread PUBLIC .)

add_library(lazypoline SHARED 
                            sud.cpp 
                            zpoline.cpp 
                            asm_syscall_hook.asm 
                            lazypoline.cpp 
                            syscall_names.c 
                            vIrTuAlIzE_sIgNaLs.cpp 
                            restore_selector_trampoline.asm 
)
target_include_directories(lazypoline PUBLIC .)
target_compile_options(lazypoline PUBLIC ${LAZYPOLINE_OPTIONS})
target_link_options(lazypoline PUBLIC ${COMMON_LINK_OPTIONS})
target_link_libraries(lazypoline PUBLIC setup_thread "-lpthread")

add_library(bootstrap SHARED bootstrap_runtime.cpp)
target_compile_options(bootstrap PUBLIC ${LAZYPOLINE_OPTIONS})
target_link_libraries(bootstrap PUBLIC "-ldl")
target_link_options(bootstrap PUBLIC ${COMMON_LINK_OPTIONS})

add_executable(main main.cpp)
target_compile_options(main PUBLIC ${LAZYPOLINE_OPTIONS})
target_link_options(main PUBLIC ${COMMON_LINK_OPTIONS})
target_link_libraries(main PUBLIC "-lpthread")
