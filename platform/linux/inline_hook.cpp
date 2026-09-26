#include "inline_hook.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <cstdlib>
#include "flaway/utils/no_log.h"

static int s_ih_diag_fd = -1;
static void ih_diag(const char* msg) {
    if (s_ih_diag_fd < 0) {
        const char* home = getenv("HOME");
        if (!home) return;
        char path[512];
        snprintf(path, sizeof(path), "%s/.minecraft/flaway_diag.txt", home);
        s_ih_diag_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }
    if (s_ih_diag_fd >= 0) {
        write(s_ih_diag_fd, msg, strlen(msg));
        write(s_ih_diag_fd, "\n", 1);
    }
}

#define MAX_HOOKS 16

struct hook_entry {
    void* target;
    void* detour;
    void* trampoline;
    uint8_t original_bytes[64];
    size_t hook_size;
    bool installed;
    bool has_rip_rel;
};

static hook_entry s_hooks[MAX_HOOKS];
static int s_hook_count = 0;
static int get_instruction_length(const uint8_t* code, bool* is_rip_rel = nullptr) {
    if (is_rip_rel) *is_rip_rel = false;
    if (!code) return 0;

    const uint8_t* p = code;
    int len = 0;
    bool has_modrm = false;
    int imm_size = 0;
    bool pref66 = false;
    bool rex_w = false;
    uint8_t op;

    // ---- Step 1: legacy prefixes (max 4) ----
    for (int i = 0; i < 4; i++) {
        switch (*p) {
            case 0x66: pref66 = true; break;
            case 0xF2: case 0xF3: case 0x2E: case 0x36:
            case 0x3E: case 0x26: case 0x64: case 0x65: case 0x67:
            case 0xF0: break;  // LOCK prefix — skip like other legacy prefixes
            default: goto done_legacy;
        }
        p++; len++;
    }
  done_legacy:
    if (len >= 15) return 0;

    // ---- Step 2: REX prefix ----
    if ((*p & 0xF0) == 0x40) {
        rex_w = (*p & 8) != 0;
        p++; len++;
        if (len >= 15) return 0;
        if (*p == 0x66) { pref66 = true; p++; len++; if (len >= 15) return 0; }
        else if (*p == 0xF2 || *p == 0xF3) { p++; len++; if (len >= 15) return 0; }
    }

    // ---- Step 3: EVEX prefix (AVX-512) ----
    // EVEX: 0x62 + 3-byte payload. Always has ModRM. May have 3-byte opcode (0x0F 0x38/0x3A).
    if (*p == 0x62) {
        p += 4; len += 4;
        if (len >= 15) return 0;
        if (*p == 0x0F) {
            p++; len++;
            if (len >= 15) return 0;
            uint8_t op2 = *p++;
            len++;
            if (op2 == 0x38 || op2 == 0x3A) {
                if (len >= 15) return 0;
                p++; len++;
            }
        } else {
            p++; len++;
        }
        has_modrm = true;
        goto modrm;
    }

    // ---- Step 4: VEX prefix (AVX) ----
    // VEX encoding: [prefixes] [VEX] [opcode] [ModRM] [SIB/disp/imm]
    if (*p == 0xC4) {
        if (len + 3 >= 15) return 0;
        p += 3; len += 3;
        if (len >= 15) return 0;
        op = *p++; len++; has_modrm = true; goto modrm;
    }
    if (*p == 0xC5) {
        if (len + 2 >= 15) return 0;
        p += 2; len += 2;
        if (len >= 15) return 0;
        op = *p++; len++; has_modrm = true; goto modrm;
    }

    // ---- Step 5: opcode ----
    op = *p++; len++;
    if (len >= 15) return 0;

    // ---- Short-form immediates ----
    // ALU AL, imm8
    if (op == 0x04 || op == 0x0C || op == 0x14 || op == 0x1C ||
        op == 0x24 || op == 0x2C || op == 0x34 || op == 0x3C) return len + 1;
    // ALU eAX, imm32 / imm16
    if (op == 0x05 || op == 0x0D || op == 0x15 || op == 0x1D ||
        op == 0x25 || op == 0x2D || op == 0x35 || op == 0x3D) {
        return len + (pref66 ? 2 : 4);
    }
    // JMP rel8
    if (op == 0xEB) return len + 1;
    // Jcc rel8 (0x70-0x7F)
    if (op >= 0x70 && op <= 0x7F) return len + 1;
    // PUSH/POP reg (0x50-0x5F)
    if (op >= 0x50 && op <= 0x5F) return len;
    // MOV r8, imm8 (0xB0-0xB7)
    if (op >= 0xB0 && op <= 0xB7) return len + 1;
    // MOV r, imm (0xB8-0xBF): imm64 with REX.W, imm16 with 0x66, else imm32
    if (op >= 0xB8 && op <= 0xBF) {
        if (rex_w) return len + 8;
        if (pref66) return len + 2;
        return len + 4;
    }
    // PUSH imm8 / imm32
    if (op == 0x6A) return len + 1;
    if (op == 0x68) return len + (pref66 ? 2 : 4);

    // ---- Single-byte no-modrm, no-imm ----
    if (op == 0xC3 || op == 0xCB || op == 0xC9 || op == 0xCF ||
        op == 0x90 || op == 0xCC || op == 0xCE ||
        op == 0xF2 || op == 0xF3 || op == 0xF0) return len;
    if (op == 0xC2 || op == 0xCA) return len + 2; // ret imm16
    if (op == 0xE2 || op == 0xE0 || op == 0xE1) return len + 1; // LOOP
    if (op == 0xC8) return len + 3; // ENTER
    if (op == 0xCD) return len + 1; // INT

    // ---- CALL/JMP rel32 ----
    if (op == 0xE8 || op == 0xE9) {
        if (is_rip_rel) *is_rip_rel = true;
        return len + 4;
    }

    // ---- IMUL r, r/m, imm ----
    if (op == 0x6B) { has_modrm = true; imm_size = 1; goto modrm; }
    if (op == 0x69) { has_modrm = true; imm_size = pref66 ? 2 : 4; goto modrm; }

    // ---- Two-byte opcode (0x0F xx / 0x0F 0x38 xx / 0x0F 0x3A xx) ----
    if (op == 0x0F) {
        uint8_t op2 = *p++; len++;
        if (len >= 15) return 0;

        // 3-byte opcodes
        if (op2 == 0x38 || op2 == 0x3A) {
            p++; len++; // op3
            if (len >= 15) return 0;
            has_modrm = true;
            if (op2 == 0x3A) imm_size = 1;
            goto modrm;
        }

        // Jcc rel32
        if (op2 >= 0x80 && op2 <= 0x8F) {
            if (is_rip_rel) *is_rip_rel = true;
            return len + 4;
        }

        // Known no-ModRM 0x0F opcodes
        switch (op2) {
        case 0x05: case 0x06: case 0x07: case 0x08: case 0x09:
        case 0x0A: case 0x0B: case 0x0E:
        case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
        case 0x77:
        case 0xA2: case 0xA8: case 0xA9: case 0xAA:
                return len;
        }

        // Opcodes with imm8 after ModRM
        if (op2 == 0xA4 || op2 == 0xAC || op2 == 0xBA || op2 == 0xC2 || op2 == 0xC6)
            imm_size = 1;

        has_modrm = true;
        goto modrm;
    }

    // ---- All remaining opcodes ----
    switch (op) {
        case 0x00: case 0x01: case 0x02: case 0x03:
        case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x28: case 0x29: case 0x2A: case 0x2B:
        case 0x30: case 0x31: case 0x32: case 0x33:
        case 0x38: case 0x39: case 0x3A: case 0x3B:
        case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0x8C: case 0x8D: case 0x8E: case 0x8F:
        case 0xC0: case 0xC1:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3:
        case 0xF6: case 0xF7:
        case 0xFE: case 0xFF:
            has_modrm = true; break;

        case 0x80: case 0x82: case 0x83: case 0xC6:
            has_modrm = true; imm_size = 1; break;
        case 0x81: case 0xC7:
            has_modrm = true; imm_size = pref66 ? 2 : 4; break;
    }

    if (!has_modrm) {
        return len;
    }

modrm:
    // ---- Step 5: ModRM + SIB + displacement ----
    if (len >= 15) return 0;
    uint8_t modrm_byte = *p++; len++;
    if (len >= 15) return 0;
    int mod = (modrm_byte >> 6) & 3;
    int rm  = modrm_byte & 7;

    if (mod == 0 && rm == 5) {
        if (is_rip_rel) *is_rip_rel = true;
        len += 4;
    } else if (mod == 1) { p++; len++; }
    else if (mod == 2) { len += 4; }

    if (rm == 4 && mod != 3) {
        uint8_t sib = *p++; len++;
        int base = sib & 7;
        if (base == 5 && mod == 0) len += 4;
    }

    // ---- Step 6: immediate ----
    len += imm_size;
    if (len > 15) len = 15;
    return len;
}

static bool is_rip_relative(const uint8_t* instr) {
    bool result = false;
    get_instruction_length(instr, &result);
    return result;
}

static bool set_mem_permissions(void* addr, size_t size, int prot) {
    if (!addr || size == 0) return false;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    uintptr_t page_start = (uintptr_t)addr & ~(page_size - 1);
    size_t region_size = ((uintptr_t)addr + size - page_start + page_size - 1) & ~(page_size - 1);
    return mprotect((void*)page_start, region_size, prot) == 0;
}

static bool write_abs_jmp(void* target, void* destination) {
    uint8_t jmp_code[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
    };
    memcpy(jmp_code + 6, &destination, 8);

    if (!set_mem_permissions(target, 14, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "write_abs_jmp: mprotect FAILED at %p: %s", target, strerror(errno));
        ih_diag(buf);
        return false;
    }
    
    memcpy(target, jmp_code, 14);
    __builtin___clear_cache((char*)target, (char*)target + 14);
    
    set_mem_permissions(target, 14, PROT_READ | PROT_EXEC); 
    return true;
}

namespace linux_hook {

bool make_writable(void* address, size_t size) { return set_mem_permissions(address, size, PROT_READ | PROT_WRITE | PROT_EXEC); }
bool make_executable(void* address, size_t size) { return set_mem_permissions(address, size, PROT_READ | PROT_EXEC); }
bool make_rwx(void* address, size_t size) { return set_mem_permissions(address, size, PROT_READ | PROT_WRITE | PROT_EXEC); }

void* install_hook(void* target, void* detour) {
    ih_diag("install_hook: entered");
    if (!target || !detour) { ih_diag("install_hook: null target or detour"); return nullptr; }

    {
        char buf[256];
        snprintf(buf, sizeof(buf), "install_hook: target=%p detour=%p", target, detour);
        ih_diag(buf);
    }

    // Check if already hooked (scan all slots, not just up to s_hook_count,
    // because remove_hook may have freed a slot below the high-water mark)
    for (int i = 0; i < s_hook_count; i++) {
        if (s_hooks[i].target == target && s_hooks[i].installed) return s_hooks[i].trampoline;
    }

    // Find first unused slot (freed by remove_hook)
    int slot = -1;
    for (int i = 0; i < s_hook_count; i++) {
        if (!s_hooks[i].installed) { slot = i; break; }
    }
    // If no free slot, grow the count (up to limit)
    if (slot < 0) {
        if (s_hook_count >= MAX_HOOKS) return nullptr;
        slot = s_hook_count++;
    }
    hook_entry& entry = s_hooks[slot];
    memset(&entry, 0, sizeof(entry));
    entry.target = target;
    entry.detour = detour;

    size_t copied = 0;
    bool has_rip_rel = false;
    
    while (copied < 14) {
        const uint8_t* instr = (const uint8_t*)target + copied;
        bool is_rip = false;
        int ilen = get_instruction_length(instr, &is_rip);
        
        if (ilen <= 0 || ilen > 15) { ilen = 1; }
        if (is_rip) { has_rip_rel = true; }
        copied += ilen;
        if (copied >= 64) break;
    }

    entry.hook_size = copied;
    entry.has_rip_rel = has_rip_rel;
    memcpy(entry.original_bytes, target, copied);

    constexpr size_t TRAMP_SIZE = 4096;
    void* trampoline = nullptr;
    uintptr_t target_addr = (uintptr_t)target;
    
    const uintptr_t search_range = 0x7FF00000; 
    uintptr_t search_start = (target_addr > search_range) ? target_addr - search_range : 0x10000;
    uintptr_t search_end = target_addr + search_range;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    
    for (uintptr_t addr = (search_start + page_size - 1) & ~(page_size - 1); addr < search_end; addr += page_size) {
        void* p = mmap((void*)addr, TRAMP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED) {
            trampoline = p;
            break;
        }
    }

    {
        char buf[256];
        snprintf(buf, sizeof(buf), "install_hook: mmap result=%s (range %p..%p)",
            trampoline ? "OK" : "FALLBACK",
            (void*)search_start, (void*)search_end);
        ih_diag(buf);
    }

    if (!trampoline) {
        ih_diag("install_hook: fallback mmap (no nearby), trying any address");
        trampoline = mmap(nullptr, TRAMP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (trampoline == MAP_FAILED) {
            ih_diag("install_hook: mmap fallback FAILED");
            return nullptr;
        }
    }

    uint8_t* tramp = (uint8_t*)trampoline;
    memcpy(tramp, entry.original_bytes, copied);
    
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "install_hook: trampoline=%p hook_size=%zu has_rip_rel=%d", trampoline, copied, (int)has_rip_rel);
        ih_diag(buf);
        // Dump the first 14 bytes at the target so we can see what we're hooking
        char hex[64] = {};
        for (size_t i = 0; i < 14 && i < copied; i++)
            snprintf(hex + i*3, 4, "%02x ", entry.original_bytes[i]);
        char hexmsg[256];
        snprintf(hexmsg, sizeof(hexmsg), "install_hook: target bytes: %s", hex);
        ih_diag(hexmsg);
    }
    
    if (has_rip_rel) {
        size_t off = 0;
        while (off < copied) {
            const uint8_t* orig_instr = (const uint8_t*)target + off;
            uint8_t* tramp_instr = tramp + off;
            
            if (is_rip_relative(orig_instr)) {
                int ilen = get_instruction_length(orig_instr);
                if (ilen >= 5) {
                    int32_t orig_disp;
                    // FIX: Displacement is always the last 4 bytes of a 32-bit rel instruction
                    int disp_offset = ilen - 4; 
                    memcpy(&orig_disp, orig_instr + disp_offset, 4);
                    
                    uintptr_t orig_next = (uintptr_t)target + off + ilen;
                    uintptr_t abs_addr = orig_next + (intptr_t)orig_disp;
                    
                    uintptr_t new_next = (uintptr_t)trampoline + off + ilen;
                    intptr_t new_disp = (intptr_t)(abs_addr - new_next);
                    
                    if (new_disp < INT32_MIN || new_disp > INT32_MAX) {
                        munmap(trampoline, TRAMP_SIZE);
                        return nullptr;
                    }
                    
                    int32_t corrected_disp = (int32_t)new_disp;
                    memcpy(tramp_instr + disp_offset, &corrected_disp, 4);
                }
            }
            int ilen = get_instruction_length(orig_instr);
            off += (ilen > 0) ? ilen : 1;
        }
    }

    void* continue_addr = (uint8_t*)target + copied;
    if (!write_abs_jmp(tramp + copied, continue_addr)) {
        ih_diag("install_hook: write_abs_jmp to trampoline FAILED");
        munmap(trampoline, TRAMP_SIZE);
        return nullptr;
    }

    __builtin___clear_cache((char*)trampoline, (char*)trampoline + TRAMP_SIZE);

    if (!set_mem_permissions(trampoline, TRAMP_SIZE, PROT_READ | PROT_EXEC)) {
        ih_diag("install_hook: set_mem_permissions trampoline FAILED");
        munmap(trampoline, TRAMP_SIZE);
        return nullptr;
    }

    entry.trampoline = trampoline;
    
    if (!write_abs_jmp(target, detour)) {
        ih_diag("install_hook: write_abs_jmp to target FAILED");
        munmap(trampoline, TRAMP_SIZE);
        return nullptr;
    }

    entry.installed = true;
    ih_diag("install_hook: SUCCESS");
    return trampoline;
}

bool remove_hook(void* target) {
    if (!target) return false;

    for (int i = 0; i < s_hook_count; i++) {
        if (s_hooks[i].target == target && s_hooks[i].installed) {
            hook_entry& entry = s_hooks[i];
            // Restore the original bytes so the target function runs without
            // the detour. The trampoline is intentionally NOT unmapped: a
            // thread already past the target's entry (e.g. the render thread
            // mid-swap) may still be executing through it, and unmapping it
            // right before the .so is dlclose'd would crash that thread.
            if (set_mem_permissions(target, entry.hook_size, PROT_READ | PROT_WRITE | PROT_EXEC)) {
                memcpy(target, entry.original_bytes, entry.hook_size);
                __builtin___clear_cache((char*)target, (char*)target + entry.hook_size);
                set_mem_permissions(target, entry.hook_size, PROT_READ | PROT_EXEC);
            }
            entry.installed = false;
            return true;
        }
    }
    return false;
}

void* get_original(void* target) {
    if (!target) return nullptr;
    for (int i = 0; i < s_hook_count; i++) {
        if (s_hooks[i].target == target) return s_hooks[i].installed ? s_hooks[i].trampoline : nullptr;
    }
    return nullptr;
}
}