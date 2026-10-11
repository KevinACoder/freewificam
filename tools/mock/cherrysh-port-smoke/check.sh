#!/bin/sh
# @file    check.sh
# @brief   Wrap the CherrySH host smoke into container cases, one set per backend.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
#
# The smoke binaries are built by their own Makefile under tests/host/ - the
# container must not grow a second copy. Each backend runs the same input
# vector (tests/host/cherrysh-port-smoke/input.txt) and the assertions are
# made on the shell's own byte stream with the SGR colour codes stripped.
#
# The expected strings are upstream sources, never this build's behaviour: the
# not-found text is the literal from chry_shell.c, the prompt is the segment
# construction from chry_shell.c, and the two listing counts are computed by
# the vendored help command from the tables themselves (so a table that lost
# or gained an entry fails this case).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
suite=cherrysh-port-smoke
vector=$root/tests/host/cherrysh-port-smoke/input.txt

failures=0

case_line() {
    if [ "$2" = "1" ]; then
        printf 'CASE %s/%s PASS\n' "$suite" "$1"
    else
        printf 'CASE %s/%s FAIL\n' "$suite" "$1"
        failures=$((failures + 1))
    fi
}

# $1 = backend name
run_backend() {
    backend=$1
    binary=$root/tests/host/cherrysh-port-smoke/build/$backend/cherrysh-smoke-$backend

    if [ ! -x "$binary" ]; then
        echo "MOCK-BUILD $suite: $binary was not built" >&2
        exit 2
    fi

    log=$(mktemp)
    text=$(mktemp)
    set +e
    timeout 20 "$binary" < "$vector" > "$log" 2>&1
    code=$?
    set -e
    sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$log" > "$text"

    # prompt: the whole prompt line, plain text once the colours are stripped
    prompt=1
    grep -qF 'user@freewificam:/$ ' "$text" || prompt=0
    case_line "$backend-prompt" "$prompt"

    # help: the listing proves the tables this build links (4 commands:
    # clear/help/shsize/exit, 2 variables: PATH/ZERO). The counts are the
    # decisive part; 'shsize' never appears as typed input.
    help=1
    grep -qF 'total function 4' "$text" || help=0
    grep -qF 'total variable 2' "$text" || help=0
    grep -qF 'shsize' "$text" || help=0
    case_line "$backend-help" "$help"

    # completion: TAB on 'cl' must submit a real command; a broken completion
    # chain submits the literal 'cl' and the shell answers "cl: command not
    # found" (upstream not-found text).
    completion=1
    grep -qF 'clear' "$text" || completion=0
    if grep -qF 'cl: command not found' "$text"; then completion=0; fi
    case_line "$backend-completion" "$completion"

    # notfound: the upstream literal, with the typed name in front
    notfound=1
    grep -qF 'nosuchcmd: command not found' "$text" || notfound=0
    case_line "$backend-notfound" "$notfound"

    # exit: the command ends the run through the host seam, exit code 0
    exit_ok=0
    if [ "$code" = 0 ]; then exit_ok=1; fi
    case_line "$backend-exit" "$exit_ok"

    echo "$suite/$backend: exit=$code"
    rm -f "$log" "$text"
}

run_backend threadx
run_backend freertos

echo "$suite: failures=$failures"
[ "$failures" = 0 ]
