#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Автоматически находим JAVA_HOME, если он не задан
if [ -z "${JAVA_HOME:-}" ]; then
    JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$(command -v javac || echo /usr/bin/javac)")")")
    # Если не нашлось, используем дефолтный путь из предыдущих логов
    if [ ! -f "${JAVA_HOME}/include/jni.h" ]; then
        JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
    fi
    export JAVA_HOME
fi

msg() { echo -e "\033[1;34m[*]\033[0m $*"; }
ok()  { echo -e "\033[1;32m[+]\033[0m $*"; }
err() { echo -e "\033[1;31m[-]\033[0m $*"; }

if [ ! -f "${JAVA_HOME}/include/jni.h" ]; then
    err "cannot locate jni.h under JAVA_HOME=${JAVA_HOME} - install a JDK or export JAVA_HOME"
    exit 1
fi

clean() {
    msg "cleaning..."
    make -C "$SCRIPT_DIR" clean
    ok "done"
}

build_so() {
    msg "building flaway.so (Linux Release)..."
    msg "using JAVA_HOME: $JAVA_HOME"
    make -C "$SCRIPT_DIR" clean
    # JOBS must be passed explicitly: the Makefile's `all` re-launches itself
    # with -j$(JOBS) (JOBS ?= 2), which discards a plain -j$(nproc).
    make -C "$SCRIPT_DIR" -j"$(nproc)" JOBS="$(nproc)"
    ok "flaway.so -> ${BUILD_DIR}/flaway.so"
}

all() {
    build_so
    ok "build complete!"
}

case "${1:-all}" in
    so|dll)       build_so ;;
    all)       all ;;
    clean)     clean ;;
    *)
        echo "usage: $0 [all|so|clean]"
        echo ""
        echo "  all        build flaway.so (default)"
        echo "  so         build only flaway.so"
        echo "  clean      remove build artifacts"
        exit 1
        ;;
esac