#include "zpoline.h"

#include "lazypoline.h"
#include "sud.h"
#include "rigtorp_spinlock.h"

void init_zpoline() {
    /* allocate memory at virtual address 0 */
	auto zeropage = (volatile uint8_t* volatile) mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS| MAP_PRIVATE|MAP_FIXED, -1, 0);
    if (zeropage == MAP_FAILED) {
        perror("mmap(NULL)");
		fprintf(stderr, "NOTE: /proc/sys/vm/mmap_min_addr should be set to 0\n");
		exit(1);
    }

    const int num_syscalls = 512;
    // insert `num_syscalls` nops
    for (int i = 0; i < num_syscalls; i++) {
		assert(num_syscalls == 512); // important for constant below
		if (i >= 0x19b) {
			if (i % 2 == 0)	
				zeropage[i] = 0x66; // single-byte nop
			else
				zeropage[i] = 0x90; // start of 2-byte nop
		} else {
			int x = i % 3;
			switch (x) {
			case 0:
				zeropage[i] = 0xeb; // rip += 0x66 (short jump) to next short jump
				break;
			case 1:
				zeropage[i] = 0x66; // 2-byte nop -> short jump
				break;
			case 2:
				zeropage[i] = 0x90;	// single-byte nop -> short jump
				break;
			}
		}
    }
    
    /* 
	 * put code for jumping to asm_syscall_hook.
	 *
	 * here we embed the following code.
	 *
	 * push   %rax
	 * movabs asm_syscall_hook, %rax 
	 * jmpq   *%rax
	 *
	 */

	/*
	 * save %rax on stack before overwriting
	 * with "movabs asm_syscall_hook, %rax",
	 * and the saved value is resumed in asm_syscall_hook.
	 */
	// 50                      push   %rax
	zeropage[num_syscalls + 0x0] = 0x50;

	// 48 b8 [64-bit addr (8-byte)]   movabs asm_syscall_hook, %rax
	zeropage[num_syscalls + 0x1] = 0x48;
	zeropage[num_syscalls + 0x2] = 0xb8;
	zeropage[num_syscalls + 0x3] = ((uint64_t) asm_syscall_hook >> (8 * 0)) & 0xff;
	zeropage[num_syscalls + 0x4] = ((uint64_t) asm_syscall_hook >> (8 * 1)) & 0xff;
	zeropage[num_syscalls + 0x5] = ((uint64_t) asm_syscall_hook >> (8 * 2)) & 0xff;
	zeropage[num_syscalls + 0x6] = ((uint64_t) asm_syscall_hook >> (8 * 3)) & 0xff;
	zeropage[num_syscalls + 0x7] = ((uint64_t) asm_syscall_hook >> (8 * 4)) & 0xff;
	zeropage[num_syscalls + 0x8] = ((uint64_t) asm_syscall_hook >> (8 * 5)) & 0xff;
	zeropage[num_syscalls + 0x9] = ((uint64_t) asm_syscall_hook >> (8 * 6)) & 0xff;
	zeropage[num_syscalls + 0xa] = ((uint64_t) asm_syscall_hook >> (8 * 7)) & 0xff;

	// ff e0                   jmpq   *%rax
	zeropage[num_syscalls + 0xb] = 0xff;
	zeropage[num_syscalls + 0xc] = 0xe0;

    // PROT_EXEC alone here gives XOM!
    ASSERT_ELSE_PERROR(mprotect((void* volatile)zeropage, 0x1000, PROT_READ|PROT_EXEC) == 0);
}

// called from `asm_syscall_hook` with unblocked SUD
extern "C" long zpoline_syscall_handler(int64_t rdi, int64_t rsi,
    int64_t rdx, int64_t r10,
    int64_t r8, int64_t r9,
	const int64_t rax,
    int64_t rip_after_syscall [[maybe_unused]],
	uint64_t* const should_emulate
) {
	return syscall_emulate(rax, rdi, rsi, rdx, r10, r8, r9, should_emulate);
}

// "shitty" globul, but its shared-ness will
// automatically be tied to CLONE_VM, which is what we want
static spinlock rewrite_lock;

void rewrite_syscall_inst(uint16_t* syscall_addr) {
    void* syscall_page = __builtin_align_down(syscall_addr, 0x1000);

	std::lock_guard guard{rewrite_lock};
	if (*syscall_addr == 0xD0FF)
		return;
	// page has to stay executable because other threads might be executing this page right now
	int perms = PROT_WRITE|PROT_EXEC;
#if COMPAT_NONDEP_APP
	perms |= PROT_READ;
#endif
    ASSERT_ELSE_PERROR(mprotect(syscall_page, 0x1000, perms) == 0);
	// if the below fails, check whether your vdso is larger than a page
	*syscall_addr = 0xD0FF;
#if not COMPAT_NONDEP_APP
	perms = PROT_READ|PROT_EXEC;
    ASSERT_ELSE_PERROR(mprotect(syscall_page, 0x1000, perms) == 0);
#endif
    // fprintf(stderr, "Rewrote syscall at %p\n", syscall_addr);
}
