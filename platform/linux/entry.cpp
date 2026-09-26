#include <jni.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include "x11_helper.h"
#include "../../flaway/flaway.h"
#include "../../flaway/hooks/Hook.h"
#include "../../flaway/utils/logger.h"
#include "flaway/utils/no_log.h"

static void diag(const char* msg) {
    const char* home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.minecraft/flaway_diag.txt", home);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        write(fd, "\n", 1);
        close(fd);
    }
}

static void __attribute__((constructor)) flaway_init()
{
    diag("=== CONSTRUCTOR START ===");

    Dl_info info;
    void* self = (void*)(uintptr_t)flaway_init;
    if (dladdr(self, &info)) {
        char buf[512];
        snprintf(buf, sizeof(buf), "dladdr dli_fname=%s", info.dli_fname);
        diag(buf);
    } else {
        diag("dladdr FAILED");
    }

    diag("calling linux_hook::init...");
    bool ok = linux_hook::init();
    diag(ok ? "linux_hook::init OK" : "linux_hook::init FAILED");
    diag("=== CONSTRUCTOR END ===");
}

static void __attribute__((destructor)) flaway_shutdown()
{
    (void)flaway::instance;
}
