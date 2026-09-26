#!/bin/bash
set -e

SO_PATH="$(cd "$(dirname "$0")" && pwd)/build/flaway.so"

if [ ! -f "$SO_PATH" ]; then
    echo "[-] $SO_PATH not found — run 'make' first"
    exit 1
fi

# Перезапуск с sudo, если не рут
if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

TARGET_INFO=$(ps --no-headers -eo pid,rss,comm | awk '$3 == "java" && $2 / 1024 >= 700 { printf "%s %.0fMB\n", $1, $2/1024 }' | sort -k2 -nr | head -1)

if [ -z "$TARGET_INFO" ]; then
    echo "[-] Game process not found"
    exit 1
fi

TARGET_PID=$(echo "$TARGET_INFO" | awk '{print $1}')

# CRITICAL: "set scheduler-locking on" freezes every thread of the game
# while we run dlopen(). Without it, the Netty/JNA threads keep executing
# during the injection and their own concurrent dlopen()/dlerror() races
# with ours inside the dynamic linker -> SIGSEGV in Netty Client IO,
# which aborts the injection mid-way and leaves the swap hook dead.
# With locking on, the game is fully parked while flaway.so loads, so
# nothing can race. detach resumes all threads normally.
GDB_OUT=$(gdb -p "$TARGET_PID" -batch \
    -ex "set pagination off" \
    -ex "set print thread-events off" \
    -ex "set scheduler-locking on" \
    -ex "handle SIGSEGV nostop noprint pass" \
    -ex "set \$h = (void*)dlopen(\"$SO_PATH\", 1)" \
    -ex "printf \"dlopen_handle = %p\\n\", \$h" \
    -ex "set \$e = (char*)dlerror()" \
    -ex "printf \"dlerror = %s\\n\", \$e" \
    -ex "set scheduler-locking off" \
    -ex "detach" 2>&1 || true)

if printf '%s' "$GDB_OUT" | grep -q "dlopen_handle = 0x0"; then
    echo "[-] INJECTION FAILED: dlopen вернул NULL"
    exit 1
fi

echo "[+] Injected"