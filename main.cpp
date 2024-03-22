#include <sys/wait.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdint.h>
#include <ucontext.h>
#include <assert.h>
#include <fenv.h>
#include <xmmintrin.h>

#include <iterator>

void handle_sigfpe(int signo) {
    assert(signo == SIGFPE);
    fprintf(stderr, "Got a SIGFPE!\n");
}

void handle_sigill(int signo, siginfo_t*, void* ucontextv) {
    assert(signo == SIGILL);
    fprintf(stderr, "Got a SIGILL!\n");

    const auto uctxt = (ucontext_t*) ucontextv;
	const auto gregs = uctxt->uc_mcontext.gregs;
    gregs[REG_RIP] += 2;
}

void* thread_start(void*) {
    for (int i = 0; i < 5; i++) {
        fprintf(stderr, "Hello from thread!\n");
        sleep(1);
    }
    return NULL;
}

int main() {
    fprintf(stderr, "Hello world!\n");
    fprintf(stderr, "Hello world!\n");
    fprintf(stderr, "Hello world!\n");
    fprintf(stderr, "Bye bye now!\n");

    struct sigaction act = {};
    act.sa_handler = handle_sigfpe;
    assert(sigaction(SIGFPE, &act, NULL) == 0);

    sigset_t mask;
    assert(sigprocmask(SIG_SETMASK, NULL, &mask) == 0);
    assert(sigismember(&mask, SIGFPE) == 0);

    raise(SIGFPE);
    raise(SIGFPE);
    raise(SIGFPE);
    raise(SIGFPE);
    raise(SIGFPE);
    raise(SIGFPE);

    fprintf(stderr, "Good times!\n");

    // now trigger a signal without `raise`
    // so we can test how we enter the signal handling path from unprivileged code

    act = {};
    act.sa_sigaction = handle_sigill;
    act.sa_flags |= SA_SIGINFO;
    assert(sigemptyset(&act.sa_mask) == 0);
    assert(sigaction(SIGILL, &act, NULL) == 0);

    struct sigaction oldact = {};
    assert(sigaction(SIGILL, NULL, &oldact) == 0);
    assert(oldact.sa_sigaction == handle_sigill);

    asm("ud2");

    act = {};
    act.sa_handler = SIG_DFL;
    assert(sigaction(SIGILL, &act, NULL) == 0);
    assert(sigaction(SIGFPE, &act, NULL) == 0);

    // test vdso syscalls

    struct timespec t;
    assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0);
    fprintf(stderr, "current thread time: %lus, %luns\n", t.tv_sec, t.tv_nsec);

    assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0);
    fprintf(stderr, "current thread time: %lus, %luns\n", t.tv_sec, t.tv_nsec);

    assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0);
    fprintf(stderr, "current thread time: %lus, %luns\n", t.tv_sec, t.tv_nsec);

    unsigned int cpu;
    unsigned int node;
    assert(getcpu(&cpu, &node) == 0);
    fprintf(stderr, "Current cpu: %d, current NUMA node: %d\n", cpu, node);

    fflush(stdout);
    fflush(stderr);
    if (pid_t child = fork()) {
        assert(child > 0);
        // parent process

        fprintf(stderr, "[%d] Parent going to sleep!\n", getpid());
        sleep(5);
        fprintf(stderr, "[%d] Parent woke up!\n", getpid());
    } else {
        // child process

        fprintf(stderr, "[%d] Child going to sleep!\n", getpid());
        sleep(10);
        fprintf(stderr, "[%d] Child woke up!\n", getpid());
        exit(0);
    }

    pthread_t thread;
    auto result = pthread_create(&thread, NULL, thread_start, NULL);
    assert(result == 0);

    result = pthread_join(thread, NULL);
    assert(result == 0);

    wait(NULL);
}

