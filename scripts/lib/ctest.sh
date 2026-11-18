# Shared boilerplate of the host C tests. A test script is just
#
#   #!/usr/bin/env bash
#   source "$(dirname "$0")/lib/ctest.sh"
#   ctest_run -I framework/include tests/test_x.c apps/x/firmware/x_core.c
#
# Everything is built with the project's warning set and with ASan/UBSan, and a sanitizer report fails the test.
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
cd "$(dirname "${BASH_SOURCE[1]}")/.."
CTEST_BIN="$(mktemp "${TMPDIR:-/tmp}/esphook-ctest.XXXXXX")"
trap 'rm -f "$CTEST_BIN"' EXIT

# ctest_build <cc args...>: compile into $CTEST_BIN. Put libraries (-lm, cJSON) after the sources.
ctest_build() {
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer "$@" -o "$CTEST_BIN"
}
# ctest_run <cc args...>: build, then run the program once.
ctest_run() {
    ctest_build "$@"
    "$CTEST_BIN"
}
