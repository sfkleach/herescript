#!/usr/bin/env bash
# Smoke tests for direct invocation of herescript (outside the shebang path).
# These cases cannot be covered by the functest YAML harness because that
# harness always invokes herescript via a script's shebang line, whereas the
# kernel guarantees the script path is present in that path. The cases here
# exercise the argument-count guard that protects main() from invalid direct
# invocations.
set -euo pipefail

HERESCRIPT="${1:-_build/herescript}"
PASS=0
FAIL=0

run_test() {
    local description="$1"
    local expected_exit="$2"
    local expected_stderr="$3"
    shift 3
    local actual_stderr
    local actual_exit=0
    actual_stderr=$(2>&1 "$@") || actual_exit=$?
    if [[ "$actual_exit" -ne "$expected_exit" ]]; then
        echo "FAIL  $description"
        echo "      expected exit $expected_exit, got $actual_exit"
        echo "      stderr: $actual_stderr"
        FAIL=$((FAIL + 1))
    elif [[ -n "$expected_stderr" && "$actual_stderr" != *"$expected_stderr"* ]]; then
        echo "FAIL  $description"
        echo "      expected stderr to contain: $expected_stderr"
        echo "      stderr: $actual_stderr"
        FAIL=$((FAIL + 1))
    else
        echo "PASS  $description"
        PASS=$((PASS + 1))
    fi
}

# No arguments at all.
run_test "no arguments produces error" \
    1 "herescript: no script specified" \
    "$HERESCRIPT"

# One argument that is not --help (executable given, script missing).
run_test "executable only (no script) produces error" \
    1 "herescript: no script specified" \
    "$HERESCRIPT" python3

# --help exits cleanly.
run_test "--help exits with code 0" \
    0 "Usage:" \
    "$HERESCRIPT" --help

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
