#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Автоматически находим JAVA_HOME, если он не задан
if [ -z "${JAVA_HOME:-}" ]; then
    JAVA_HOME=$(dirname $(dirname $(readlink -f $(which javac) 2>/dev/null || echo "/usr/bin/javac") 2>/dev/null))
    # Если не нашлось, используем дефолтный путь из предыдущих логов
    if [ ! -f "${JAVA_HOME}/include/jni.h" ]; then
        JAVA_HOME="/usr/lib/jvm/java-17-openjdk"
    fi
    export JAVA_HOME
fi

msg() { echo -e "\033[1;34m[*]\033[0m $*"; }
ok()  { echo -e "\033[1;32m[+]\033[0m $*"; }
err() { echo -e "\033[1;31m[-]\033[0m $*"; }

clean() {
    msg "cleaning..."
    make -C "$SCRIPT_DIR" clean
    ok "done"
}

build_so() {
    msg "building flaway.so (Linux Release)..."
    msg "using JAVA_HOME: $JAVA_HOME"
    make -C "$SCRIPT_DIR" clean
    make -C "$SCRIPT_DIR" -j$(nproc)
    ok "flaway.so -> ${BUILD_DIR}/flaway.so"
}

build_injector() {
    msg "building inject_linux..."
    make -C "$SCRIPT_DIR" injector
    ok "injector -> ${BUILD_DIR}/inject_linux"
}

all() {
    build_so
    build_injector
    ok "build complete!"
}

case "${1:-all}" in
    so|dll)       build_so ;;
    injector)  build_injector ;;
    all)       all ;;
    clean)     clean ;;
    *)
        echo "usage: $0 [all|so|injector|clean]"
        echo ""
        echo "  all        build flaway.so and injector (default)"
        echo "  so         build only flaway.so"
        echo "  injector   build only inject_linux"
        echo "  clean      remove build artifacts"
        exit 1
        ;;
esac