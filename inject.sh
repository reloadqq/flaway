#!/usr/bin/env bash
set -e

clear

banner=$(cat <<'EOF'
__ _                           
  / _| | __ ___      ____ _ _   _ 
 | |_| |/ _` | \ \ /\ / / _` | | | |
 |  _| | (_| |\ V  V / (_| | |_| |
 |_| |_|\__,_| \_/\_/ \__,_|\__, |
                            |___/ 
EOF
)

red=(255 40 0)
green=(0 255 150)

rgb() {
  local step=$1 total=$2 width=$3
  local char=$4
  local t r g b
  if (( width > 1 )); then
    t=$(( step * (width - 1) / (total - 1) ))
    r=$(( red[0] + (green[0] - red[0]) * t / (width - 1) ))
    g=$(( red[1] + (green[1] - red[1]) * t / (width - 1) ))
    b=$(( red[2] + (green[2] - red[2]) * t / (width - 1) ))
  else
    r=${green[0]}; g=${green[1]}; b=${green[2]}
  fi
  printf '\033[38;2;%d;%d;%dm%s\033[0m' "$r" "$g" "$b" "$char"
}

print_gradient() {
  local text="$1" len i ch
  len=${#text}
  for (( i = 0; i < len; i++ )); do
    ch="${text:$i:1}"
    if [[ "$ch" == $'\n' ]]; then
      echo ""
    else
      rgb "$i" "$len" "$len" "$ch"
    fi
  done
  [ "${text:len-1}" != $'\n' ] && echo ""
}

print_gradient "$banner"

cyan=(0 255 220)
ok=(0 200 120)
err=(255 60 60)

log_ok()  { printf '\033[38;2;%d;%d;%dm[+]\033[0m ' "${cyan[0]}" "${cyan[1]}" "${cyan[2]}"; print_gradient "$1" ""; }
log_err() { printf '\033[38;2;255;60;60m[-]\033[0m '; print_gradient "$1" ""; }

SO_PATH="$(cd "$(dirname "$0")" && pwd)/build/flaway.so"

if [ ! -f "$SO_PATH" ]; then
    log_err "$SO_PATH not found — run 'make' first"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    log_err "need root — restarting with sudo"
    exec sudo "$0" "$@"
fi

TARGET_INFO=$(ps --no-headers -eo pid,rss,comm | awk '$3 == "java" && $2 / 1024 >= 700 { printf "%s %.0fMB\n", $1, $2/1024 }' | sort -k2 -nr | head -1)

if [ -z "$TARGET_INFO" ]; then
    log_err "Game process not found"
    exit 1
fi

TARGET_PID=$(echo "$TARGET_INFO" | awk '{print $1}')
log_ok "Found game process (pid $TARGET_PID)"

log_ok "Starting injection via gdb (scheduler-locking on)"
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
    log_err "INJECTION FAILED: dlopen returned NULL"
    exit 1
fi

log_ok "Injected"
