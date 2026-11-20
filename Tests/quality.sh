#!/usr/bin/env bash
# Host quality gates: clang, sanitizers, coverage, ARM cross-compile, cppcheck.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && { pwd -W 2>/dev/null || pwd; })
cd "$ROOT"

PYTHON=${PYTHON:-python}
CC=${CC:-gcc}
BUILD=Tests/integration/.build/quality

CORE=(
    Xcp/Xcp.c Xcp/Xcp_Cmd.c Xcp/Xcp_Std.c Xcp/Xcp_DaqCfg.c
    Xcp/Xcp_DaqRun.c Xcp/Xcp_DaqStim.c Xcp/Xcp_SeedKey.c
    Xcp/Xcp_CalPage.c Xcp/Xcp_FlashProg.c Xcp/Xcp_Resume.c
    Xcp/XcpOnCan.c Xcp/XcpOnFr.c Xcp/XcpOnEth.c Xcp/XcpOnCdd.c
)
HOST=(Tests/test_stubs.c Tests/test_main.c Gen/can/Xcp_Cfg.c)
CFLAGS=(
    -std=c99 -g -Wall -Wextra -Wno-unused-parameter
    -Werror=implicit-function-declaration
    -ITests/stubs -ITests -IXcp -IGen/can
    -include Tests/test_config.h
)

usage() {
    echo "usage: $0 [host|matrix|sanitize|coverage|arm|cppcheck|all]" >&2
}

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "quality: missing $1" >&2; exit 2; }
}

gen() {
    "$PYTHON" Tools/xcpgen/xcpgen.py --profile can --out Gen/can
}

host() {
    need "$CC"
    gen
    mkdir -p "$BUILD"
    "$CC" "${CFLAGS[@]}" -o "$BUILD/xcp_test_host" "${CORE[@]}" "${HOST[@]}"
    "$BUILD/xcp_test_host"
}

matrix() {
    CC="$CC" "$PYTHON" Tests/build_matrix.py
}

sanitize() {
    need clang
    gen
    mkdir -p "$BUILD"
    clang "${CFLAGS[@]}" -O1 -fno-omit-frame-pointer \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$BUILD/xcp_test_san" "${CORE[@]}" "${HOST[@]}"
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        "$BUILD/xcp_test_san"
}

coverage() {
    need gcc
    need gcovr
    gen
    local build=Tests/integration/.build/coverage
    local sources=()
    for src in "${CORE[@]}" "${HOST[@]}"; do
        sources+=("$ROOT/$src")
    done
    rm -rf "$build"
    mkdir -p "$build"
    (
        cd "$build"
        gcc --coverage -fprofile-abs-path -std=c99 -g \
            -Wall -Wextra -Wno-unused-parameter \
            -Werror=implicit-function-declaration \
            -I"$ROOT/Tests/stubs" -I"$ROOT/Tests" -I"$ROOT/Xcp" -I"$ROOT/Gen/can" \
            -include "$ROOT/Tests/test_config.h" \
            "${sources[@]}" -o xcp_test_can
        ./xcp_test_can
        gcovr --root "$ROOT" --filter "$ROOT/Xcp/" \
            --xml coverage.xml --html-details coverage.html \
            --print-summary --fail-under-line 80 --fail-under-function 90
    )
}

arm() {
    need arm-none-eabi-gcc
    gen
    local cpu
    for cpu in cortex-m0 cortex-m4; do
        echo "arm: $cpu"
        arm-none-eabi-gcc -std=c99 -mcpu="$cpu" -mthumb -Os \
            -Wall -Wextra -Werror -Wno-unused-parameter \
            -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
            -ITests/stubs -ITests -IXcp -IGen/can \
            -include Tests/test_config.h -fsyntax-only \
            "${CORE[@]}" Gen/can/Xcp_Cfg.c
    done
}

cppcheck_gate() {
    need cppcheck
    # Define the critical-section hooks empty so older cppcheck parsers do not
    # reject the function-like macros from Gen/<profile>/Xcp_Cfg.h.
    cppcheck --std=c99 --enable=warning,performance,portability \
        --error-exitcode=1 --inline-suppr \
        --suppress=missingIncludeSystem --suppress=unusedFunction \
        --suppress=normalCheckLevelMaxBranches \
        -D'XCP_DTO_ENTER_CRITICAL()=' -D'XCP_DTO_EXIT_CRITICAL()=' \
        -IXcp -ITests -ITests/stubs -IGen/can \
        --include=Tests/test_config.h Xcp
}

case "${1:-all}" in
    host) host ;;
    matrix) matrix ;;
    sanitize) sanitize ;;
    coverage) coverage ;;
    arm) arm ;;
    cppcheck) cppcheck_gate ;;
    all)
        CC=clang
        host
        matrix
        sanitize
        coverage
        arm
        cppcheck_gate
        ;;
    *) usage; exit 2 ;;
esac
