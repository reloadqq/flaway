#include "crash_dump.h"

#include <signal.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace crash_dump {

namespace {

// Executable ranges of the libraries we care about. A SIGSEGV whose PC lands
// inside one of these is treated as a crash in our native path; everything
// else is passed straight through to the JVM's own handler (which relies on
// SIGSEGV for implicit null checks and crash reporting).
struct Range {
    uintptr_t start;
    uintptr_t end;
    uintptr_t base;      // load base of the owning library (first mapping)
    char name[64];
};
Range g_ranges[96];
int g_range_count = 0;
uintptr_t g_flaway_base = 0;

// Teardown phase tracker: unhook_all() bumps this before every destructive
// step. On a crash the handler appends it to the signature file so we can see
// WHICH teardown step died (async-signal-safe volatile write).
volatile int g_phase = -1;

// Async-signal-safe crash signature sink. Opened once at init() (never in the
// handler), written only on a real crash. No fopen/malloc/backtrace in the
// handler: those can deadlock a crashing JVM and turn a recoverable fault into
// a guaranteed kill (this is exactly what the old crash-dump logging did and
// it raced the delta-JRE guard-page fault storm). Content is tiny (~40 bytes).
static int g_crash_fd = -1;

// Record only the first few faults; during a fault storm the subsequent
// intentional JVM faults must not flood the file (they're expected). The limit
// is >1 because an early, recoverable fault (JVM implicit/guard-page hit) would
// otherwise permanently mask the real crash we actually need to diagnose.
static volatile sig_atomic_t g_crash_count = 0;
static const int k_max_crashes = 4;

struct sigaction g_prev[NSIG];
bool g_prev_valid[NSIG] = {false};
volatile bool g_installed = false;

const int k_signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };

static bool is_in_our_libs(uintptr_t pc)
{
    for (int i = 0; i < g_range_count; i++)
        if (pc >= g_ranges[i].start && pc < g_ranges[i].end)
            return true;
    return false;
}

// Append the /proc/self/maps line covering each requested address, prefixed
// with its tag (maps_fault= / maps_src= / maps_dst=). One pass over maps and
// strictly async-signal-safe: open/read/write/snprintf only — no fopen, no
// malloc. A raw fault address is useless on its own (ASLR); these lines say
// WHAT the page is: heap chunk, thread-stack guard, PROT_NONE reservation,
// file mapping — plus the mapping size, which tells us whether a 1MB copy
// would still fit inside it.
static void append_vmas(const uintptr_t addrs[3], const char* const tags[3])
{
    int mfd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (mfd < 0)
        return;
    bool found[3] = {false, false, false};
    char chunk[4096];
    char line[512];
    char out[640];
    int llen = 0;
    bool skip = false;
    for (;;)
    {
        ssize_t r = read(mfd, chunk, sizeof(chunk));
        if (r <= 0)
            break;
        for (ssize_t i = 0; i < r; i++)
        {
            char c = chunk[i];
            if (skip) { if (c == '\n') skip = false; continue; }
            if (c != '\n')
            {
                if (llen < (int)sizeof(line) - 1) line[llen++] = c;
                else skip = true;
                continue;
            }
            line[llen] = 0;
            llen = 0;

            uintptr_t s = 0, e = 0;
            const char* p = line;
            bool any = false;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))
            { s = (s << 4) | (uintptr_t)(*p <= '9' ? *p - '0' : *p - 'a' + 10); p++; any = true; }
            if (!any || *p != '-') continue;
            p++;
            e = 0;
            any = false;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))
            { e = (e << 4) | (uintptr_t)(*p <= '9' ? *p - '0' : *p - 'a' + 10); p++; any = true; }
            if (!any) continue;

            for (int k = 0; k < 3; k++)
            {
                if (found[k] || addrs[k] == 0 || addrs[k] < s || addrs[k] >= e)
                    continue;
                found[k] = true;
                int n = snprintf(out, sizeof(out), "%s%s\n", tags[k], line);
                if (n > 0)
                    (void)write(g_crash_fd, out, (size_t)(n < (int)sizeof(out) ? n : (int)sizeof(out) - 1));
            }
            if (found[0] && found[1] && found[2]) { close(mfd); return; }
        }
    }
    close(mfd);
    for (int k = 0; k < 3; k++)
        if (!found[k] && addrs[k])
        {
            int n = snprintf(out, sizeof(out), "%s<no-vma>\n", tags[k]);
            if (n > 0)
                (void)write(g_crash_fd, out, (size_t)(n < (int)sizeof(out) ? n : (int)sizeof(out) - 1));
        }
}

static void dump_crash(int sig, siginfo_t* si, void* uc)
{
    // Tiny async-signal-safe signature: signal, PC, fault address, teardown
    // phase, thread id. Only written on a real crash; this is the sole
    // client-side file produced by the cheat (regular logging stays removed).
    (void)uc;
    if (g_crash_count >= k_max_crashes || g_crash_fd < 0)
        return;
    g_crash_count = (sig_atomic_t)(g_crash_count + 1);

    uintptr_t pc = 0;
    ucontext_t* uctx = (ucontext_t*)uc;
    if (uctx)
    {
#if defined(__x86_64__)
        pc = (uintptr_t)uctx->uc_mcontext.gregs[REG_RIP];
#else
        pc = (uintptr_t)uctx->uc_mcontext.gregs[REG_PC];
#endif
    }
    uintptr_t fault_addr = si ? (uintptr_t)si->si_addr : 0;

    // Key registers for crash triage. The most common failure here is a
    // memcpy/memmove/memset in the swap path, so log the classic copy args:
    // rdi=dest, rsi=src, rdx=len (plus rbx, which glibc's AVX memcpy keeps as
    // a copy of the length) and the address that actually faulted.
    // With these, a next crash identifies exactly which native buffer died.
    uintptr_t reg_rdi = 0, reg_rsi = 0, reg_rdx = 0, reg_rbx = 0;
    if (uctx)
    {
#if defined(__x86_64__)
        reg_rdi = (uintptr_t)uctx->uc_mcontext.gregs[REG_RDI];
        reg_rsi = (uintptr_t)uctx->uc_mcontext.gregs[REG_RSI];
        reg_rdx = (uintptr_t)uctx->uc_mcontext.gregs[REG_RDX];
        reg_rbx = (uintptr_t)uctx->uc_mcontext.gregs[REG_RBX];
#else
        reg_rdi = (uintptr_t)uctx->uc_mcontext.gregs[REG_R0];
        reg_rsi = (uintptr_t)uctx->uc_mcontext.gregs[REG_R1];
        reg_rdx = (uintptr_t)uctx->uc_mcontext.gregs[REG_R2];
#endif
    }

    char buf[384];
    // Attribute the crashing PC and the fault address to their owning library
    // (name + offset from the library load base). Without this the raw pc is
    // useless (ASLR randomizes every mapping per session). flaway.so base is
    // also included so a crash inside our .so can be addr2line'd directly.
    const char* in_lib = "?";
    uintptr_t in_off = 0;
    for (int i = 0; i < g_range_count; i++)
        if (pc >= g_ranges[i].start && pc < g_ranges[i].end)
        { in_lib = g_ranges[i].name; in_off = pc - g_ranges[i].base; break; }
    const char* ad_lib = "?";
    uintptr_t ad_off = 0;
    for (int i = 0; i < g_range_count; i++)
        if (fault_addr >= g_ranges[i].start && fault_addr < g_ranges[i].end)
        { ad_lib = g_ranges[i].name; ad_off = fault_addr - g_ranges[i].base; break; }

    int n = snprintf(buf, sizeof(buf),
        "sig=%d pc=0x%lx in=%s+0x%lx fbase=0x%lx addr=0x%lx addr_in=%s+0x%lx rdi=0x%lx rsi=0x%lx rdx=0x%lx rbx=0x%lx phase=%d tid=%ld\n",
        sig, (unsigned long)pc, in_lib, (unsigned long)in_off, (unsigned long)g_flaway_base,
        (unsigned long)fault_addr, ad_lib, (unsigned long)ad_off,
        (unsigned long)reg_rdi, (unsigned long)reg_rsi, (unsigned long)reg_rdx,
        (unsigned long)reg_rbx,
        (int)g_phase, (long)syscall(SYS_gettid));
    if (n > 0)
        (void)write(g_crash_fd, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));

    // Identify the faulting page and the copy's src/dst regions.
    {
        const uintptr_t addrs[3] = { fault_addr, reg_rsi, reg_rdi };
        const char* const tags[3] = { "maps_fault=", "maps_src=", "maps_dst=" };
        append_vmas(addrs, tags);
    }
}

static void signal_handler(int sig, siginfo_t* si, void* uc)
{
    static volatile sig_atomic_t s_in_handler = 0;
    if (s_in_handler)
    {
        // Recursive fault (e.g. a fault hit while we were dumping, or the JVM
        // handler longjmp'd out of a previous call leaving the flag set).
        // Do NOT dump (backtrace/malloc are not re-entrant here) and do NOT
        // _exit: during a fault storm this killed the game. Hand it straight
        // to the JVM's own handler so it can recover and the game keeps
        // running.
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, sig);
        sigprocmask(SIG_UNBLOCK, &set, nullptr);
        if (g_prev_valid[sig])
        {
            struct sigaction& prev = g_prev[sig];
            if ((prev.sa_flags & SA_SIGINFO) && prev.sa_sigaction)
                prev.sa_sigaction(sig, si, uc);
            else if (prev.sa_handler == SIG_DFL)
            {
                // SIG_DFL is NULL: the branch below would CALL NULL.
                signal(sig, SIG_DFL);
                raise(sig);
            }
            else if (prev.sa_handler != SIG_IGN)
                prev.sa_handler(sig);
            else { signal(sig, SIG_DFL); raise(sig); }
        }
        else { signal(sig, SIG_DFL); raise(sig); }
        return;
    }
    s_in_handler = 1;

    uintptr_t pc = 0;
    ucontext_t* uctx = (ucontext_t*)uc;
    if (uctx)
    {
#if defined(__x86_64__)
        pc = (uintptr_t)uctx->uc_mcontext.gregs[REG_RIP];
#else
        pc = (uintptr_t)uctx->uc_mcontext.gregs[REG_PC];
#endif
    }

    uintptr_t fault_addr = si ? (uintptr_t)si->si_addr : 0;

    // Capture real crashes where the fault address is NOT a JVM implicit null
    // check (HotSpot uses SIGSEGV at addresses 0x0-0x1000 for implicit null
    // checks and recovers automatically).  We allow the PC to be anywhere in
    // any executable library because crashes in libc's memcpy/memmove are
    // called from our code.
    bool dump = false;
    if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL) {
        if (fault_addr >= 0x10000 && is_in_our_libs(pc))
            dump = true;
    }
    if (dump)
        dump_crash(sig, si, uc);

    // Delegate to the previously-installed handler (the JVM's) with the
    // ORIGINAL ucontext so its crash reporter still produces an hs_err file.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    sigprocmask(SIG_UNBLOCK, &set, nullptr);

    s_in_handler = 0;
    if (g_prev_valid[sig])
    {
        struct sigaction& prev = g_prev[sig];
        if ((prev.sa_flags & SA_SIGINFO) && prev.sa_sigaction)
            prev.sa_sigaction(sig, si, uc);
        else if (prev.sa_handler == SIG_DFL)
        {
            signal(sig, SIG_DFL);
            raise(sig);
        }
        else if (prev.sa_handler != SIG_IGN)
            prev.sa_handler(sig);
    }
    else
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

} // namespace

void set_phase(int phase)
{
    g_phase = phase;
}

void init()
{
    if (g_installed) return;
    g_installed = true;

    // Open the crash signature sink once here (never inside the handler).
    {
        const char* home = getenv("HOME");
        if (home && *home)
        {
            char path[512];
            int m = snprintf(path, sizeof(path), "%s/.minecraft/flaway_crash.txt", home);
            if (m > 0 && m < (int)sizeof(path))
                g_crash_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        }
    }

    // Record executable mappings for every loaded library, with their load base,
    // so a crash can be attributed to a concrete library+offset.
    struct RawMap { uintptr_t start, end, off; bool exec; char path[512]; };
    RawMap tmp[256];
    int tmpc = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (f)
    {
        char line[1024];
        while (fgets(line, sizeof(line), f) && tmpc < 256)
        {
            uintptr_t start = 0, end = 0, off = 0;
            char perms[8] = {0};
            char path[512] = {0};
            // Collect EVERY mapping, not just the executable ones: the mapping
            // with off == 0 (the ELF header) is r--p and is required to compute
            // the true load base below.
            if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %511s", &start, &end, perms, &off, path) >= 4)
            {
                const char* slash = strrchr(path, '/');
                const char* base = slash ? slash + 1 : path;
                RawMap& m = tmp[tmpc++];
                m.start = start; m.end = end; m.off = off;
                m.exec = (perms[2] == 'x');
                snprintf(m.path, sizeof(m.path), "%s", base);
            }
        }
        fclose(f);
    }

    // Compute the load base of each library (its mapping with offset 0, else
    // its lowest mapping) and build the deduplicated range table.
    for (int i = 0; i < tmpc && g_range_count < (int)(sizeof(g_ranges) / sizeof(g_ranges[0])); i++)
    {
        if (!tmp[i].exec) continue;   // ranges only for executable segments
        uintptr_t base = 0;
        for (int j = 0; j < tmpc; j++)
            if (strcmp(tmp[j].path, tmp[i].path) == 0 && tmp[j].off == 0)
            { base = tmp[j].start; break; }
        if (!base)
            for (int j = 0; j < tmpc; j++)
                if (strcmp(tmp[j].path, tmp[i].path) == 0 && (base == 0 || tmp[j].start < base))
                    base = tmp[j].start;

        // One entry per executable mapping (a library may have several), so a
        // PC landing in any of them resolves to a real range.
        bool dup = false;
        for (int r = 0; r < g_range_count; r++)
            if (g_ranges[r].start == tmp[i].start && g_ranges[r].end == tmp[i].end)
            { dup = true; break; }
        if (dup) continue;

        g_ranges[g_range_count].start = tmp[i].start;
        g_ranges[g_range_count].end = tmp[i].end;
        g_ranges[g_range_count].base = base;
        snprintf(g_ranges[g_range_count].name, sizeof(g_ranges[g_range_count].name), "%s",
                 tmp[i].path[0] ? tmp[i].path : "[anon]");
        if (strcmp(tmp[i].path, "flaway.so") == 0)
            g_flaway_base = base;
        g_range_count++;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    for (int s : k_signals)
    {
        if (sigaction(s, &sa, &g_prev[s]) == 0)
            g_prev_valid[s] = true;
    }
}

// Called right before the .so is unloaded. Our handlers live inside the .so;
// leaving them installed would make the game jump into unmapped code on the
// next JVM SIGSEGV (HotSpot uses SIGSEGV for implicit null checks, so a fault
// is expected and frequent). Restore the handlers that were active before
// init() so the JVM's own crash recovery keeps working after unload.
void shutdown()
{
    if (!g_installed) return;
    g_installed = false;
    for (int s : k_signals)
    {
        if (g_prev_valid[s])
            sigaction(s, &g_prev[s], nullptr);
        else
            signal(s, SIG_DFL);
        g_prev_valid[s] = false;
    }
    if (g_crash_fd >= 0)
    {
        close(g_crash_fd);
        g_crash_fd = -1;
    }
}

} // namespace crash_dump
