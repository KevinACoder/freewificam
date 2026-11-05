#!/bin/sh
# check-build-contract.sh - the compiler-semantics contract is intact.
#
# The imported driver trees in this image are Linux code: they are written
# against KBUILD_CFLAGS, which switches off strict aliasing, strict overflow
# and null-pointer deletion for the whole tree.  Compiling them without that
# list is not a tuning difference, it is a different language - in rtw88 it
# silently corrupts the aggregate head's TX descriptor checksum and the chip
# then drops every multi-frame URB
# (runs/iterations/0.0.4/2026-11-05-RTOS-AGG-03).
#
# The rule this script enforces, for the profile being built:
#   1. KCONTRACT_CFLAGS has exactly one definition (build/Makefile) and carries
#      the kernel's three flags;
#   2. every effective C flags set in the build - whatever it is called and
#      however many levels of $(...) it goes through - carries those flags;
#   3. no set pins its own optimisation level: every set's -O token is the one
#      $(OPT) resolved to for this profile, so a debug profile (-O0/-Og) reaches
#      the driver units too.
#
# Rule 2 is checked on the expanded values (./make.rtos dump-vars), not on the
# text of the .mk files, so a new flag world cannot pass by spelling.  The
# runtime half of the same guarantee is the aggregate-head checksum self-check
# in tests/board/rtos/app/cases/net_mdk_netio.c, which recomputes the checksum
# from the bytes that actually left the host.
#
# Usage: tools/build/check-build-contract.sh [PROFILE=<name>]
# Exit 0 when it holds, 1 with one line per violation otherwise.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
mk="$root/tests/board/rtos/build/Makefile"
profile=${PROFILE:-$(sed -n 's/^[[:space:]]*CONFIG[[:space:]]*:=[[:space:]]*//p' \
    "$root/tests/board/rtos/config.mk" | head -n1)}
flags='-fno-strict-aliasing -fno-strict-overflow -fno-delete-null-pointer-checks'

# The logical lines of a makefile: backslash continuations joined and runs of
# blanks collapsed, so a definition is compared as the single value make sees.
logical() {
    awk '
        { line = $0
          while (line ~ /\\$/) {
              sub(/\\$/, "", line)
              if ((getline nxt) <= 0) break
              line = line " " nxt
          }
          gsub(/[ \t]+/, " ", line)
          print line }
    ' "$1"
}

fails=0
err() {
    printf 'build-contract: %s\n' "$*" >&2
    fails=$((fails + 1))
}

# 1. One definition, carrying the kernel's three flags.
n=$(grep -c '^KCONTRACT_CFLAGS[[:space:]]*:=' "$mk" || true)
[ "$n" -eq 1 ] || err "KCONTRACT_CFLAGS must be defined exactly once in build/Makefile (found $n)"
contract=$(logical "$mk" | grep '^KCONTRACT_CFLAGS[[:space:]]*:=' || true)
for flag in $flags; do
    case " $contract " in
        *" $flag "*) ;;
        *) err "KCONTRACT_CFLAGS lacks $flag" ;;
    esac
done

# 2 and 3, on the effective values for this profile.  dump-vars expands every
# make variable the platform Makefile defines; what it prints is what the
# compiler is handed.
# --no-print-directory: a sub-make prints "Entering directory" on stdout,
# which would land in front of the JSON.
vars=$(MAKEFLAGS= PROFILE="$profile" make -f "$root/tools/build/rtos.mk" \
	--no-print-directory dump-vars 2>/dev/null) || {
    # The profile's own makefile refused to evaluate (a missing firmware blob,
    # a missing third-party tree): the image cannot build either way, so this
    # is a failure here too - but say which line stopped it.
    err "the build configuration of $profile does not even evaluate:"
    MAKEFLAGS= PROFILE="$profile" make -f "$root/tools/build/rtos.mk" \
	--no-print-directory dump-vars 2>&1 | grep -E '\*\*\*' | head -n 3 >&2 || true
    vars=
}
if [ -n "$vars" ]; then
    printf '%s\n' "$vars" | python3 -c '
import json, sys
flags = sys.argv[1].split()
d = json.load(sys.stdin)
prof = list(d["profiles"].values())[0]
vars = prof["vars"]
base = vars.get("BASE_CFLAGS", "")
base_opt = next((t for t in base.split() if t.startswith("-O")), "")
bad = 0
if not base_opt:
    print("build-contract: BASE_CFLAGS carries no -O token", file=sys.stderr)
    bad += 1
for name in sorted(vars):
    if not name.endswith("_CFLAGS"):
        continue
    if name in ("ASM_CFLAGS", "KCONTRACT_CFLAGS") or name.startswith("HOST_"):
        continue           # assembler, the definition itself, host tier
    value = vars[name]
    if not value.strip():
        continue           # empty placeholder
    for flag in flags:
        if flag not in value.split():
            print("build-contract: " + name + " does not carry " + flag, file=sys.stderr)
            bad += 1
    opts = sorted({t for t in value.split() if t.startswith("-O")})
    for o in opts:
        if o != base_opt:
            print("build-contract: " + name + " pins " + o +
                  " while this profile builds at " + base_opt +
                  "; it must follow $(OPT)", file=sys.stderr)
            bad += 1
sys.exit(1 if bad else 0)
' "$flags" || fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
    printf 'build-contract: FAIL (%d violation(s)); see tools/build/check-build-contract.sh\n' "$fails" >&2
    exit 1
fi
printf 'build-contract: ok (profile=%s, flags=%s)\n' "$profile" "$contract"
