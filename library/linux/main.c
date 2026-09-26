/*
 * Linux ptrace-based injector for flaway.so
 *
 * Usage: ./inject_linux [pid] [/path/to/flaway.so]
 *        ./inject_linux <name> [/path/to/flaway.so]   (search by process name)
 *
 * Method:
 *   1. ptrace attach
 *   2. Find dlopen() addr in target via /proc/pid/maps + ELF offset
 *   3. Inject shellcode: dlopen(path, RTLD_NOW|RTLD_GLOBAL) → int3
 *   4. Read rax = dlopen result
 *   5. Detach
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>

// ─── x86_64 shellcode templates ────────────────────────────────────────────

/* dlopen(path, RTLD_NOW|RTLD_GLOBAL) → rax then int3.
 * If dlopen fails (rax==0), calls dlerror() so we get error string ptr.
 * All indirect via [rip+disp] to avoid mov r64,imm64 on code pages.
 * Starts with CLD for ABI safety.
 *
 * Layout:
 *   [0..32]  = shellcode (33 bytes)
 *   [33..40] = dlopen address (8 bytes)
 *   [41..48] = dlerror address (8 bytes)
 *   [49..]   = path string
 *
 * Shellcode:
 *   cld                                          ; 1B
 *   lea    rdi, [rip + path_off]                ; 7B
 *   mov    rsi, RTLD_NOW|RTLD_GLOBAL (=0x102)   ; 7B
 *   call   [rip + dlopen_ptr_off]               ; 6B
 *   test   rax, rax                              ; 3B
 *   jne    +6 (skip dlerror)                     ; 2B
 *   call   [rip + dlerror_ptr_off]              ; 6B
 *   int3                                         ; 1B
 */
static const uint8_t shellcode_dlopen_template[] = {
    0xFC,                                      // [0]:   cld
    0x48, 0x8D, 0x3D,                         // [1-3]: lea rdi,[rip+disp32]
    0x00, 0x00, 0x00, 0x00,                   // [4-7]: disp32 (path)
    0x48, 0xC7, 0xC6, 0x02, 0x01, 0x00, 0x00, // [8-14]: mov rsi,0x102 (RTLD_NOW|GLOBAL)
    0xFF, 0x15,                               // [15-16]: call [rip+disp32]
    0x00, 0x00, 0x00, 0x00,                   // [17-20]: disp32 (dlopen ptr)
    0x48, 0x85, 0xC0,                         // [21-23]: test rax,rax
    0x75, 0x06,                               // [24-25]: jne +6 → int3
    0xFF, 0x15,                               // [26-27]: call [rip+disp32]
    0x00, 0x00, 0x00, 0x00,                   // [28-31]: disp32 (dlerror ptr)
    0xCC,                                      // [32]:   int3
};
#define SC_PATH_DISP_OFF   4
#define SC_DLOPEN_DISP_OFF 17
#define SC_DLERR_DISP_OFF  28
#define SC_SHELL_SZ        33
#define SC_DLADDR_OFF      SC_SHELL_SZ           // 33 — dlopen addr
#define SC_DLERR_OFF       (SC_DLADDR_OFF + 8)   // 41 — dlerror addr
#define SC_PATH_OFF        (SC_DLERR_OFF + 8)    // 49 — path string



// ─── process helpers ──────────────────────────────────────────────────────

/* Read from target via process_vm_readv (works on any readable page) */
static int read_proc_mem(pid_t pid, void *addr, void *buf, size_t len)
{
    struct iovec local = { .iov_base = buf, .iov_len = len };
    struct iovec remote = { .iov_base = addr, .iov_len = len };
    ssize_t r = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return (r == (ssize_t)len) ? 0 : -1;
}

/* Write to target using ptrace POKEDATA — bypasses page permissions.
 * Can write to read-only and execute-only pages (like code segments).
 * Uses memcpy for alignment-safe access to source buffer. */
static int write_proc_force(pid_t pid, void *addr, const void *buf, size_t len)
{
    const uint8_t *src = (const uint8_t*)buf;
    uintptr_t dst = (uintptr_t)addr;
    // Handle unaligned head
    size_t off = dst % sizeof(unsigned long);
    if (off) {
        size_t chunk = sizeof(unsigned long) - off;
        if (chunk > len) chunk = len;
        errno = 0;
        unsigned long word = ptrace(PTRACE_PEEKDATA, pid, (void*)(dst - off), NULL);
        if (word == ULONG_MAX && errno != 0) return -1;
        unsigned char *p = (unsigned char*)&word + off;
        memcpy(p, src, chunk);
        if (ptrace(PTRACE_POKEDATA, pid, (void*)(dst - off), (void*)word) != 0)
            return -1;
        dst += chunk;
        src += chunk;
        len -= chunk;
    }

    // Write full words
    while (len >= sizeof(unsigned long)) {
        unsigned long word;
        memcpy(&word, src, sizeof(unsigned long));
        if (ptrace(PTRACE_POKEDATA, pid, (void*)dst, (void*)word) != 0)
            return -1;
        dst += sizeof(unsigned long);
        src += sizeof(unsigned long);
        len -= sizeof(unsigned long);
    }

    // Handle tail
    if (len > 0) {
        errno = 0;
        unsigned long word = ptrace(PTRACE_PEEKDATA, pid, (void*)dst, NULL);
        if (word == ULONG_MAX && errno != 0) return -1;
        memcpy(&word, src, len);
        if (ptrace(PTRACE_POKEDATA, pid, (void*)dst, (void*)word) != 0)
            return -1;
    }

    return 0;
}

/* Get base address of a library in a process from /proc/pid/maps.
 * Uses dladdr info: dli_fname is the library path.
 * Searches for the first r-xp mapping containing that path. */
static uintptr_t find_lib_base(pid_t pid, const char *lib_path)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[1024];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, lib_path) && strstr(line, "r-xp")) {
            sscanf(line, "%lx", &base);
            break;
        }
    }
    fclose(f);
    return base;
}

/* Resolve an arbitrary symbol address in target process.
 * We use dladdr() to find which library exports the symbol in OUR process,
 * compute its offset from that library's base, then add the same offset
 * to the target's base for the same library. */
static void *resolve_sym(pid_t pid, const char *sym_name, int verbose)
{
    void *addr = dlsym(RTLD_DEFAULT, sym_name);
    if (!addr) {
        if (verbose) fprintf(stderr, "[-] %s not found in our process\n", sym_name);
        return NULL;
    }
    if (verbose) printf("[*] %s @ %p\n", sym_name, addr);

    Dl_info info;
    if (!dladdr(addr, &info)) { if (verbose) fprintf(stderr, "[-] dladdr failed for %s\n", sym_name); return NULL; }
    if (!info.dli_fname) { if (verbose) fprintf(stderr, "[-] no dli_fname for %s\n", sym_name); return NULL; }

    if (verbose) printf("[*] %s is in: %s\n", sym_name, info.dli_fname);

    uintptr_t our_base = find_lib_base(getpid(), info.dli_fname);
    if (!our_base) {
        if (verbose) fprintf(stderr, "[-] our_base for %s not found\n", info.dli_fname);
        return NULL;
    }

    uintptr_t target_base = find_lib_base(pid, info.dli_fname);
    if (!target_base) {
        if (verbose) fprintf(stderr, "[-] target_base for %s not found\n", info.dli_fname);
        return NULL;
    }

    uintptr_t offset = (uintptr_t)addr - our_base;
    void *result = (void*)(target_base + offset);

    if (verbose) {
        printf("[*] our_base=0x%lx  target_base=0x%lx  offset=0x%lx\n",
               (unsigned long)our_base, (unsigned long)target_base,
               (unsigned long)offset);
        // Read first bytes to verify
        unsigned char probe[8] = {0};
        if (read_proc_mem(pid, result, probe, 8) == 0)
            printf("[*] target prologue: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   probe[0], probe[1], probe[2], probe[3],
                   probe[4], probe[5], probe[6], probe[7]);
        else
            fprintf(stderr, "[-] cannot read target memory at resolved %s!\n", sym_name);
    }

    return result;
}

/* Resolve dlopen in target (convenience wrapper) */
static void *resolve_dlopen(pid_t pid)
{
    void *addr = resolve_sym(pid, "__libc_dlopen_mode", 0);
    if (!addr) {
        addr = resolve_sym(pid, "dlopen", 1);
        printf("[*] using public dlopen\n");
    } else {
        printf("[*] using __libc_dlopen_mode\n");
    }
    return addr;
}

/* Read rax from tracee */
static uintptr_t get_rax(pid_t pid)
{
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) != 0) return 0;
    return regs.rax;
}

/* Print all registers for crash diagnostics */
static void dump_regs(pid_t pid)
{
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) != 0) return;
    fprintf(stderr, "    RAX=0x%lx  RBX=0x%lx  RCX=0x%lx\n",
            (unsigned long)regs.rax, (unsigned long)regs.rbx,
            (unsigned long)regs.rcx);
    fprintf(stderr, "    RDX=0x%lx  RSI=0x%lx  RDI=0x%lx\n",
            (unsigned long)regs.rdx, (unsigned long)regs.rsi,
            (unsigned long)regs.rdi);
    fprintf(stderr, "    RBP=0x%lx  RSP=0x%lx  RIP=0x%lx\n",
            (unsigned long)regs.rbp, (unsigned long)regs.rsp,
            (unsigned long)regs.rip);
    fprintf(stderr, "     R8=0x%lx   R9=0x%lx  R10=0x%lx\n",
            (unsigned long)regs.r8,  (unsigned long)regs.r9,
            (unsigned long)regs.r10);
    fprintf(stderr, "    R11=0x%lx  R12=0x%lx  R13=0x%lx\n",
            (unsigned long)regs.r11, (unsigned long)regs.r12,
            (unsigned long)regs.r13);
    fprintf(stderr, "    R14=0x%lx  R15=0x%lx\n",
            (unsigned long)regs.r14, (unsigned long)regs.r15);

    // Read first bytes at crash RIP to understand fault
    unsigned char probe[16] = {0};
    if (read_proc_mem(pid, (void*)regs.rip, probe, 16) == 0) {
        fprintf(stderr, "    code @RIP:");
        for (int i = 0; i < 16; i++)
            fprintf(stderr, " %02x", probe[i]);
        fprintf(stderr, "\n");
    }
}

// ─── find process by name ─────────────────────────────────────────────────

static pid_t find_process(const char *name)
{
    DIR *proc = opendir("/proc");
    if (!proc) return 0;

    struct dirent *entry;
    pid_t found = 0;
    long best_rss = -1;   /* prefer the process with most resident memory */

    while ((entry = readdir(proc))) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;

        pid_t pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        char cmd_path[64];
        snprintf(cmd_path, sizeof(cmd_path), "/proc/%d/comm", pid);
        FILE *f = fopen(cmd_path, "r");
        if (!f) continue;

        char comm[256];
        int matched = 0;
        if (fgets(comm, sizeof(comm), f)) {
            // Strip newline
            size_t len = strlen(comm);
            if (len > 0 && comm[len-1] == '\n') comm[len-1] = '\0';

            if (strcmp(comm, name) == 0)
                matched = 1;
            // Also match substring for "java" (OpenJDK uses "java" comm)
            else if (strstr(comm, name))
                matched = 1;
        }
        fclose(f);
        if (!matched) continue;

        long rss = -1;
        char status_path[64];
        snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
        FILE *st = fopen(status_path, "r");
        if (st) {
            char buf[128];
            while (fgets(buf, sizeof(buf), st)) {
                if (sscanf(buf, "VmRSS: %ld kB", &rss) == 1)
                    break;
            }
            fclose(st);
        }

        if (rss > best_rss) {
            best_rss = rss;
            found = pid;
        }
    }

    closedir(proc);
    return found;
}

// ─── main injection logic ─────────────────────────────────────────────────

/* Stop ALL threads in the target process.
 * ptrace(PTRACE_ATTACH) only stops one thread; other threads continue
 * executing and will crash if they hit overwritten code pages.
 * We iterate /proc/pid/task and attach to each thread. */
static int stop_all_threads(pid_t pid, pid_t **tids, int *n_tids)
{
    char task_path[64];
    snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);

    DIR *dir = opendir(task_path);
    if (!dir) {
        fprintf(stderr, "[-] cannot open %s\n", task_path);
        return -1;
    }

    int cap = 64, cnt = 0;
    pid_t *list = malloc(cap * sizeof(pid_t));
    if (!list) { closedir(dir); return -1; }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
        pid_t tid = atoi(entry->d_name);
        if (tid <= 0) continue;
        if (tid == pid) continue; // main thread already stopped

        if (cnt >= cap) {
            cap *= 2;
            pid_t *tmp = realloc(list, cap * sizeof(pid_t));
            if (!tmp) { free(list); closedir(dir); return -1; }
            list = tmp;
        }
        list[cnt++] = tid;
    }
    closedir(dir);

    // Attach to each extra thread
    for (int i = 0; i < cnt; i++) {
        if (ptrace(PTRACE_ATTACH, list[i], NULL, NULL) != 0) {
            fprintf(stderr, "[-] ptrace ATTACH to tid %d failed: %s\n",
                    list[i], strerror(errno));
            // Continue anyway — best-effort
            continue;
        }
        int ws;
        waitpid(list[i], &ws, 0);
    }

    *tids = list;
    *n_tids = cnt;
    return 0;
}

static int resume_all_threads(pid_t pid, pid_t *tids, int n_tids)
{
    for (int i = 0; i < n_tids; i++) {
        ptrace(PTRACE_DETACH, tids[i], NULL, NULL);
    }
    free(tids);
    return 0;
}

static int inject_library(pid_t pid, const char *so_path)
{
    printf("[*] attaching to PID %d\n", pid);

    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0) {
        fprintf(stderr, "[-] ptrace ATTACH failed: %s\n", strerror(errno));
        return 1;
    }

    int status;
    waitpid(pid, &status, 0);
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "[-] wait failed after attach\n");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    // Stop ALL other threads so they don't execute overwritten code
    pid_t *extra_tids = NULL;
    int n_extra = 0;
    stop_all_threads(pid, &extra_tids, &n_extra);
    printf("[*] stopped %d additional thread(s)\n", n_extra);

    printf("[*] resolving dlopen() and dlerror() in target process\n");

    void *dlopen_addr = resolve_dlopen(pid);
    if (!dlopen_addr) {
        fprintf(stderr, "[-] could not resolve dlopen in target\n");
        goto detach;
    }
    printf("[*] dlopen @ %p\n", dlopen_addr);

    void *dlerror_addr = resolve_sym(pid, "dlerror", 1);
    if (!dlerror_addr) {
        fprintf(stderr, "[-] could not resolve dlerror in target\n");
        goto detach;
    }
    printf("[*] dlerror @ %p\n", dlerror_addr);

    // Save registers — we'll inject at the current execution point
    struct user_regs_struct old_regs;
    ptrace(PTRACE_GETREGS, pid, NULL, &old_regs);
    printf("[*] injecting at RIP=0x%lx RSP=0x%lx\n",
           (unsigned long)old_regs.rip, (unsigned long)old_regs.rsp);

    // ===== Write dlopen payload directly to the code page =====
    void *scratch = (void*)(old_regs.rip & ~0xfffULL);

    char abs_path[PATH_MAX];
    if (!realpath(so_path, abs_path)) {
        fprintf(stderr, "[-] realpath(%s) failed: %s\n", so_path, strerror(errno));
        goto detach;
    }

    size_t path_len = strlen(abs_path) + 1;

    // Payload: shellcode + dlopen ptr(8) + dlerror ptr(8) + path
    size_t total = SC_SHELL_SZ + 8 + 8 + path_len;
    uint8_t code_buf[512];
    if (total > sizeof(code_buf)) { fprintf(stderr, "[-] payload too large\n"); goto detach; }

    // Build shellcode
    uint8_t *sc = code_buf;
    memcpy(sc, shellcode_dlopen_template, SC_SHELL_SZ);

    // LEA at bytes 1..7 (after cld), RIP at end = scratch+8, target = SC_PATH_OFF
    int32_t path_disp = (int32_t)(SC_PATH_OFF - 8);
    memcpy(sc + SC_PATH_DISP_OFF, &path_disp, 4);

    // CALL dlopen at bytes 15..20, RIP at end = scratch+21, target = SC_DLADDR_OFF
    int32_t dlopen_disp = (int32_t)(SC_DLADDR_OFF - 21);
    memcpy(sc + SC_DLOPEN_DISP_OFF, &dlopen_disp, 4);

    // CALL dlerror at bytes 26..31, RIP at end = scratch+32, target = SC_DLERR_OFF
    int32_t dlerr_disp = (int32_t)(SC_DLERR_OFF - 32);
    memcpy(sc + SC_DLERR_DISP_OFF, &dlerr_disp, 4);

    uint64_t dl_addr = (uint64_t)dlopen_addr;
    memcpy(code_buf + SC_DLADDR_OFF, &dl_addr, 8);
    uint64_t dl_err_addr = (uint64_t)dlerror_addr;
    memcpy(code_buf + SC_DLERR_OFF, &dl_err_addr, 8);
    memcpy(code_buf + SC_PATH_OFF, abs_path, path_len);

    printf("[*] total payload: %zu bytes  path=%s\n", total, abs_path);

    // Save original code page bytes at scratch
    uint8_t saved[512];
    if (total > sizeof(saved)) { fprintf(stderr, "[-] saved buffer too small\n"); goto detach; }
    if (read_proc_mem(pid, scratch, saved, total) != 0) {
        fprintf(stderr, "[-] failed to save scratch bytes\n");
        goto detach;
    }

    // Write payload via PTRACE_POKEDATA (bypasses page permissions)
    if (write_proc_force(pid, scratch, code_buf, total) != 0) {
        fprintf(stderr, "[-] failed to write payload\n");
        write_proc_force(pid, scratch, saved, total);
        goto detach;
    }

    // Set RIP to scratch and execute
    struct user_regs_struct regs = old_regs;
    regs.rip = (uintptr_t)scratch;
    // Match exec_shellcode: use original stack with 0x100 headroom
    regs.rsp = (old_regs.rsp - 0x100) & ~15ULL;
    regs.rbp = 0;  // clear frame pointer for safety
    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) != 0) {
        fprintf(stderr, "[-] PTRACE_SETREGS failed\n");
        write_proc_force(pid, scratch, saved, total);
        goto detach;
    }

    printf("[*] executing dlopen(\"%s\", ...) from code page\n", abs_path);

    ptrace(PTRACE_CONT, pid, NULL, NULL);
    waitpid(pid, &status, 0);

    uintptr_t result = 0;
    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
        result = get_rax(pid);
        printf("[+] dlopen returned 0x%lx\n", result);
        if (result != 0) {
            // Try reading as dlerror string (if dlopen failed, result is error string pointer)
            char errbuf[256] = {0};
            if (read_proc_mem(pid, (void*)result, errbuf, sizeof(errbuf)-1) == 0 && errbuf[0]) {
                // Check if it looks like a text string (not binary garbage)
                int printable = 1;
                for (int i = 0; errbuf[i] && i < 100; i++)
                    if (errbuf[i] < 32 && errbuf[i] != '\n' && errbuf[i] != '\t') { printable = 0; break; }
                if (printable)
                    fprintf(stderr, "[-] dlerror: %s\n", errbuf);
            }
        }
    } else {
        fprintf(stderr, "[-] shellcode failed (status=0x%x)\n", status);
        if (WIFSTOPPED(status)) {
            fprintf(stderr, "[-] signal %d (SIG%s)\n",
                    WSTOPSIG(status), strsignal(WSTOPSIG(status)));
            dump_regs(pid);
        }
        write_proc_force(pid, scratch, saved, total);
        ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
        goto detach;
    }

    // Restore
    write_proc_force(pid, scratch, saved, total);
    ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);

    printf("[*] detaching\n");

    // Resume extra threads before detaching main
    if (extra_tids) resume_all_threads(pid, extra_tids, n_extra);

    if (ptrace(PTRACE_DETACH, pid, NULL, NULL) != 0) {
        fprintf(stderr, "[-] ptrace DETACH failed: %s\n", strerror(errno));
        return 1;
    }

    return 0;

detach:
    if (extra_tids) resume_all_threads(pid, extra_tids, n_extra);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return 1;
}

// ─── usage ────────────────────────────────────────────────────────────────

static void print_usage(const char *argv0)
{
    printf("Usage: %s [options] <so_path>\n", argv0);
    printf("       %s [options] -p <pid>  <so_path>\n", argv0);
    printf("       %s [options] -n <name> <so_path>\n\n", argv0);
    printf("Options:\n");
    printf("  -p <pid>      Target process PID\n");
    printf("  -n <name>     Target process name (searches /proc)\n");
    printf("  -h            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -n java ./build/flaway.so\n", argv0);
    printf("  %s -p 1234  ./build/flaway.so\n", argv0);
    printf("  %s ./build/flaway.so              (auto-detect java)\n", argv0);
}

// ─── main ─────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    pid_t pid = 0;
    const char *name = NULL;
    const char *so_path = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "p:n:h")) != -1) {
        switch (opt) {
        case 'p': pid = atoi(optarg); break;
        case 'n': name = optarg; break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (optind < argc)
        so_path = argv[optind];

    if (!so_path) {
        fprintf(stderr, "[-] missing .so path\n");
        print_usage(argv[0]);
        return 1;
    }

    // Resolve PID
    if (pid == 0 && name) {
        printf("[*] searching for process \"%s\"...\n", name);
        pid = find_process(name);
        if (!pid) {
            fprintf(stderr, "[-] process \"%s\" not found\n", name);
            return 1;
        }
        printf("[+] found PID %d\n", pid);
    }

    if (pid == 0) {
        printf("[*] auto-detecting Java process...\n");
        pid = find_process("java");
        if (!pid) {
            fprintf(stderr, "[-] no Java process found.\n");
            fprintf(stderr, "    Start Minecraft first, or specify -p <pid>.\n");
            return 1;
        }
        printf("[+] found Java PID %d\n", pid);
    }

    // Verify .so exists
    if (access(so_path, F_OK) != 0) {
        fprintf(stderr, "[-] %s: %s\n", so_path, strerror(errno));
        return 1;
    }

    // Check we have CAP_SYS_PTRACE or are root
    if (getuid() != 0) {
        // ptrace may still work with YAMA configured, but warn
        fprintf(stderr, "[!] not running as root — ptrace may fail\n");
        fprintf(stderr, "    echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope\n");
    }

    printf("[+] target PID: %d\n", pid);
    printf("[+] library:    %s\n", so_path);

    return inject_library(pid, so_path);
}
