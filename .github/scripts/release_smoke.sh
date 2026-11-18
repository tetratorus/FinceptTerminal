#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# release_smoke.sh — Tier 1 (automated) of the release smoke suite.
# See docs/qa/README.md for the full suite and the manual tiers.
#
#   release_smoke.sh <binary> [build-dir] [out-dir]
#
# Runs, in order, and keeps going after a failure so one run reports everything:
#   T1-01 unit tests      ctest in <build-dir> (skipped when no build dir / no tests)
#   T1-02 self-tests      every --selftest-* flag the binary registers
#   T1-03 screen walk     --smoke-test: constructs every registered screen
# Logs and a Markdown summary go to <out-dir> (default ./release-smoke-<timestamp>).
# Exit status is non-zero when any step failed.
# ─────────────────────────────────────────────────────────────────────────────
set -uo pipefail

BINARY="${1:-}"; BUILD_DIR="${2:-}"; OUT_DIR="${3:-release-smoke-$(date +%Y%m%d-%H%M%S)}"
if [ -z "$BINARY" ] || [ ! -f "$BINARY" ]; then
    echo "usage: $0 <binary> [build-dir] [out-dir]" >&2
    exit 2
fi
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUT_DIR"
SUMMARY="$OUT_DIR/summary.md"
FAILED=0

record() { # id name status log
    printf '| %s | %s | %s | %s |\n' "$1" "$2" "$3" "$(basename "$4")" >>"$SUMMARY"
    echo "[$3] $1 $2"
    [ "$3" = "FAIL" ] && FAILED=1
    return 0
}

{
    echo "# Release smoke — automated tier"
    echo
    echo "- Binary: \`$BINARY\`"
    echo "- Host: $(uname -srm)"
    echo "- Started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo
    echo "| ID | Check | Result | Log |"
    echo "|----|-------|--------|-----|"
} >"$SUMMARY"

LOG="$OUT_DIR/T1-01-ctest.log"
if [ -n "$BUILD_DIR" ] && [ -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    if ctest --test-dir "$BUILD_DIR" --output-on-failure >"$LOG" 2>&1; then
        record T1-01 "Unit tests (ctest)" PASS "$LOG"
    else
        record T1-01 "Unit tests (ctest)" FAIL "$LOG"
    fi
else
    echo "no CTest build dir given (configure with -DFINCEPT_BUILD_TESTS=ON)" >"$LOG"
    record T1-01 "Unit tests (ctest)" SKIP "$LOG"
fi

LOG="$OUT_DIR/T1-02-selftests.log"
if bash "$HERE/ci_app_checks.sh" selftests "$BINARY" release-smoke >"$LOG" 2>&1; then
    record T1-02 "Headless self-tests ($(grep -c '^PASS ' "$LOG") passed)" PASS "$LOG"
else
    record T1-02 "Headless self-tests (failed:$(grep -o 'self-tests failed:.*' "$LOG" | cut -d: -f2-))" FAIL "$LOG"
fi

LOG="$OUT_DIR/T1-03-screen-walk.log"
if bash "$HERE/ci_app_checks.sh" smoke "$BINARY" release-smoke-walk 600 >"$LOG" 2>&1; then
    record T1-03 "Screen construction walk (--smoke-test)" PASS "$LOG"
else
    record T1-03 "Screen construction walk (last: $(grep -o '\[Smoke\] >>> constructing [^ ]*' "$LOG" | tail -1 | awk '{print $NF}'))" FAIL "$LOG"
fi

echo >>"$SUMMARY"
echo "Finished: $(date -u +%Y-%m-%dT%H:%M:%SZ). Continue with the manual tiers in docs/qa/README.md." >>"$SUMMARY"
echo "Summary: $SUMMARY"
exit "$FAILED"
