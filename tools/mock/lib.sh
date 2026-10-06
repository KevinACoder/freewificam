#!/bin/sh
# @file    lib.sh
# @brief   Classification rules of the mock container: one place that decides what a
#          suite run means.
# @author  zhugengyu
# @date    07.10.2026
# @design adapter - ref net80211:tests/run-host-tests.sh:1
#
# The runner and the container's own self test both source this file, so the rules that
# decide PASS from FAIL exist once and can be exercised on purpose-built fixtures.
#
# Expectations a suite may declare (SUITE_EXPECT in its suite.mk):
#
#   exit:0                the run must succeed
#   exit:<n>              the run must fail with exit code <n>  (an expected red)
#   exit:<n> match:<re>   as above, and the output must match the regular expression
#   signal:<n>            the run must die from signal <n>      (a designed hard crash)
#   compilefail           the run must fail - a negative source that must not build
#   self                  the suite reports its own CASE lines
#
# Verdicts: PASS (as declared), XFAIL (an expected red happened as declared), FAIL (a
# declared success did not happen, or an expected red failed differently), XPASS (an
# expected red passed - the scenario no longer reproduces, which is a failure here),
# TIMEOUT, SKIP (never acceptable).

# mk_get FILE KEY - read "KEY := value" from a declaration file (make assignment syntax,
# so a Makefile aggregator can include the same files).
mk_get() {
    sed -n "s/^[[:space:]]*${2}[[:space:]]*:=[[:space:]]*//p" "$1" | head -1
}

# mock_is_ok VERDICT - verdicts that mean "the suite behaved as declared".
mock_is_ok() {
    case "$1" in
        PASS|XFAIL) return 0 ;;
        *)          return 1 ;;
    esac
}

# mock_classify EXPECT EXIT_CODE LOG - print the verdict for a declared expectation.
mock_classify() {
    expect=$1
    code=$2
    log=$3

    case "$expect" in
    exit:*)
        want=${expect#exit:}
        want=${want%% *}
        pattern=
        case "$expect" in
            *match:*) pattern=$(printf '%s' "$expect" | sed -n 's/.*match://p') ;;
        esac

        if [ "$code" = "$want" ]; then
            if [ -n "$pattern" ] && ! grep -qE "$pattern" "$log"; then
                printf 'FAIL\n'
                return
            fi
            [ "$want" = "0" ] && printf 'PASS\n' || printf 'XFAIL\n'
        else
            [ "$code" = "0" ] && printf 'XPASS\n' || printf 'FAIL\n'
        fi
        ;;
    signal:*)
        want=${expect#signal:}
        if [ "$code" = "$((128 + want))" ]; then
            printf 'XFAIL\n'
        elif [ "$code" = "0" ]; then
            printf 'XPASS\n'
        else
            printf 'FAIL\n'
        fi
        ;;
    compilefail)
        # The negative source must not build: a non-zero compile is the expected red.
        [ "$code" != "0" ] && printf 'XFAIL\n' || printf 'XPASS\n'
        ;;
    *)
        printf 'FAIL\n'
        ;;
    esac
}

# mock_count_cases LOG - number of CASE lines in a suite log.
mock_count_cases() {
    grep -c '^CASE ' "$1" 2>/dev/null || true
}

# mock_bad_cases LOG - CASE lines whose verdict is not one of the accepted ones.
mock_bad_cases() {
    grep '^CASE ' "$1" 2>/dev/null | grep -vE ' (PASS|XFAIL)$' || true
}

# mock_verdict_of LINE - the last field of a CASE line.
mock_verdict_of() {
    printf '%s\n' "$1" | awk '{print $NF}'
}
