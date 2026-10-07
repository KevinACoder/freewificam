#!/bin/sh
# @file    run.sh
# @brief   Prove the container can go red: expected-red forms are classified as declared,
#          and deliberately broken suites make the container itself fail.
# @author  zhugengyu
# @date    07.10.2026
# @design template method - ref net80211:tests/run-host-tests.sh:1
#
# Half one exercises the classification rules directly (the rules live in the container's
# own library, so this is the same code path the runner uses). Half two drives the whole
# container over synthetic broken suites and asserts that it reports each break the way it
# is supposed to - a container that silently passes its own negative controls is the one
# failure mode a self test must not have.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
mock=$(cd "$here/.." && pwd)
fixtures=$mock/build/container-selftest/fixtures
suite=container-selftest
failures=0

. "$mock/lib.sh"

result() {
    if [ "$2" = "1" ]; then
        printf 'CASE %s/%s PASS\n' "$suite" "$1"
    else
        printf 'CASE %s/%s FAIL\n' "$suite" "$1"
        failures=$((failures + 1))
    fi
}

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# classify NAME EXPECT EXPECTED_VERDICT - run the fixture for EXPECT and compare.
classify() {
    name=$1
    expect=$2
    want=$3
    shift 3

    set +e
    "$@" > "$log" 2>&1
    code=$?
    set -e

    got=$(mock_classify "$expect" "$code" "$log" | tr -d '\n')
    if [ "$got" = "$want" ]; then
        result "$name" 1
    else
        result "$name" 0
        echo "  $name: expect=$expect exit=$code classified $got, wanted $want" >&2
    fi
}

# --- half one: the four expectation forms, plus a signature that must not match -------
classify plain_pass           'exit:0'                  PASS  "$fixtures/pass"
classify declared_red         'exit:1 match:reason=4'   XFAIL "$fixtures/fail"
classify designed_crash       'signal:11'               XFAIL "$fixtures/crash"
classify compilefail_source   'compilefail'             XFAIL \
         cc -c "$here/fixtures/compilefail.c" -o /dev/null
classify signature_mismatch   'exit:1 match:no-such-signature' FAIL "$fixtures/fail"

# --- half two: the container must fail on broken suites ------------------------------
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"; rm -f "$log"' EXIT

write_suite() {
    name=$1
    expect=$2
    run=$3
    truth=$4
    mkdir -p "$tmp/$name"
    {
        echo "SUITE_NAME := $name"
        echo "SUITE_TIMEOUT := 5"
        echo "SUITE_EXPECT := $expect"
        echo "SUITE_RUN := $run"
        echo "SUITE_SEGMENTS := -"
        echo "SUITE_WIRELESS := 0"
        if [ -n "$truth" ]; then echo "SUITE_TRUTH_SOURCE := $truth"; fi
    } > "$tmp/$name/suite.mk"
}

write_suite selftest-fail         'exit:0'  "$fixtures/fail"          "negative control"
write_suite selftest-xpass        'exit:1'  "$fixtures/pass"          "negative control"
write_suite selftest-timeout      'exit:0'  "sleep 30"                "negative control"
write_suite selftest-compilepass  'compilefail' "cc -c $here/fixtures/pass.c -o /dev/null" "negative control"

{
    echo "SUITES := selftest-fail selftest-xpass selftest-timeout selftest-compilepass"
} > "$tmp/suites.mk"

set +e
MOCK_SUITES_MK=$tmp/suites.mk MOCK_SUITE_ROOT=$tmp sh "$mock/run.sh" > "$tmp/out.log" 2>&1
rc=$?
set -e

assert_break() {
    name=$1
    log=$2
    pattern=$3
    if grep -qE "$pattern" "$log"; then
        result "$name" 1
    else
        result "$name" 0
        echo "  $name: expected /$pattern/ in the container report" >&2
        sed 's/^/    /' "$log" >&2
    fi
}

assert_break negative_control_fail        "$tmp/out.log" '^CASE selftest-fail/main FAIL'
assert_break negative_control_xpass       "$tmp/out.log" '^CASE selftest-xpass/main XPASS'
assert_break negative_control_timeout     "$tmp/out.log" '^CASE selftest-timeout/main TIMEOUT'
assert_break negative_control_compilepass "$tmp/out.log" '^CASE selftest-compilepass/main XPASS'
assert_break negative_control_exit_code   "$tmp/out.log" '^MOCK-RESULT fail'
result negative_control_exit_status "$([ "$rc" = 1 ] && echo 1 || echo 0)"

# A suite that does not declare where its expectations come from may not run at all: the
# container must refuse it as a configuration error, not test it.
write_suite selftest-nodecl 'exit:0' "$fixtures/pass" ""
{
    echo "SUITES := selftest-nodecl"
} > "$tmp/suites-nodecl.mk"

set +e
MOCK_SUITES_MK=$tmp/suites-nodecl.mk MOCK_SUITE_ROOT=$tmp sh "$mock/run.sh" > "$tmp/out-nodecl.log" 2>&1
rc=$?
set -e

assert_break negative_control_missing_truth "$tmp/out-nodecl.log" 'MOCK-CONFIG selftest-nodecl: SUITE_TRUTH_SOURCE missing'
result negative_control_missing_truth_status "$([ "$rc" = 2 ] && echo 1 || echo 0)"


# --- half three: the segment schema must refuse a broken table ------------------------
# The registry is the contract both sides print against, so a table that is incomplete or
# self-contradictory has to stop the run. Each fixture breaks exactly one rule.
mapfixtures=$tmp/maps
mkdir -p "$mapfixtures"
head -1 "$mock/segment-map.tsv" > "$mapfixtures/header.tsv"

check_map_red() {
    name=$1
    file=$2
    if sh "$mock/check-segment-map.sh" "$file" > "$mapfixtures/$name.out" 2>&1; then
        result "$name" 0
        echo "  $name: the schema check accepted a table it must refuse" >&2
    else
        result "$name" 1
    fi
}

first_row=$(sed -n '2p' "$mock/segment-map.tsv")
s5_row=$(grep -P '\tS5\t' "$mock/segment-map.tsv" | head -1)

# a row with a field missing
{ cat "$mapfixtures/header.tsv"; printf '%s\n' "$first_row" | cut -f1-13; } > "$mapfixtures/missing_field.tsv"
check_map_red segment_map_missing_field "$mapfixtures/missing_field.tsv"

# the two sides printing different field names
{ cat "$mapfixtures/header.tsv"; printf '%s\n' "$first_row" | awk -F'\t' 'BEGIN{OFS="\t"}{$11="<prefix>S1.EP0_SETUP ok=%u"; print}'; } > "$mapfixtures/template_drift.tsv"
check_map_red segment_map_template_drift "$mapfixtures/template_drift.tsv"

# the same anchor id twice
{ cat "$mapfixtures/header.tsv"; printf '%s\n' "$first_row"; printf '%s\n' "$first_row"; } > "$mapfixtures/duplicate_id.tsv"
check_map_red segment_map_duplicate_id "$mapfixtures/duplicate_id.tsv"

# a segment without a consumer claiming to be active (S5 is not built on purpose)
{ cat "$mapfixtures/header.tsv"; printf '%s\n' "$s5_row" | awk -F'\t' 'BEGIN{OFS="\t"}{$14="active"; print}'; } > "$mapfixtures/mislaid.tsv"
check_map_red segment_map_mislaid_row "$mapfixtures/mislaid.tsv"

# a vector column pointing at a file that does not exist
{ cat "$mapfixtures/header.tsv"; printf '%s\n' "$first_row" | awk -F'\t' 'BEGIN{OFS="\t"}{$8="tools/mock/vectors/S1/no-such-vector.dat"; print}'; } > "$mapfixtures/missing_vector.tsv"
check_map_red segment_map_missing_vector "$mapfixtures/missing_vector.tsv"

# --- half four: a wireless suite may not run without its reference route --------------
mkdir -p "$tmp/selftest-wireless"
{
    echo "SUITE_NAME := selftest-wireless"
    echo "SUITE_TIMEOUT := 5"
    echo "SUITE_EXPECT := exit:0"
    echo "SUITE_RUN := $fixtures/pass"
    echo "SUITE_SEGMENTS := -"
    echo "SUITE_WIRELESS := 1"
    echo "SUITE_TRUTH_SOURCE := negative control"
} > "$tmp/selftest-wireless/suite.mk"
{
    echo "SUITES := selftest-wireless"
} > "$tmp/suites-wireless.mk"

set +e
MOCK_SUITES_MK=$tmp/suites-wireless.mk MOCK_SUITE_ROOT=$tmp sh "$mock/run.sh" > "$tmp/out-wireless.log" 2>&1
rc=$?
set -e

assert_break negative_control_missing_route "$tmp/out-wireless.log" 'MOCK-CONFIG selftest-wireless: wireless suite without an ieee80211-route annotation'
result negative_control_missing_route_status "$([ "$rc" = 2 ] && echo 1 || echo 0)"

# and the container must say out loud that no wireless suite is registered, rather than
# leaving the empty set unstated
assert_break empty_wireless_set_declared "$tmp/out.log" '^MOCK-NOTE no wireless suites registered yet'

# --- half five: the vector factory's validator must refuse broken vectors ------------
# Each fixture is a mini factory tree under a temporary directory: a clean one that must
# pass, then one break per vector rule. The fixtures never touch the real vectors/ tree,
# so a deliberately bad vector can never enter the data set.
vecfixtures=$tmp/vecfactory
mkdir -p "$vecfixtures"
write_mini_factory() {
    dir=$1
    mkdir -p "$dir/vectors/S1"
    cat > "$dir/vectors/S1/S1.DOORBELL.in.001.dat" <<'DAT'
mockvec1
ver=1
seg=S1
dir=in
seq=1
anchor=S1.DOORBELL
payload_bytes=3
crc32=1b6214f6
sha256=47ffa3ea45a70b8a41c2c0825df323c00a8b7a01c1ea06083cc41dddcc001123
truth=usb2-ch9.4
source=derived:spec
captured=
redact=none
00ff01
DAT
    cat > "$dir/vectors/S1/S1.DOORBELL.exp.001.dat" <<'DAT'
mockvec1
ver=1
seg=S1
dir=exp
seq=1
anchor=S1.DOORBELL
payload_bytes=3
crc32=1b6214f6
sha256=47ffa3ea45a70b8a41c2c0825df323c00a8b7a01c1ea06083cc41dddcc001123
truth=usb2-ch9.4
source=derived:spec
captured=
redact=none
00ff01
DAT
    printf 'file\tvec\tseg\tdir\tanchor\ttruth\troute\tredact\tnorm\tnote\n' > "$dir/vectors/manifest.tsv"
    # Rows in file-name order, as the schema requires (exp sorts before in).
    for d in exp in; do
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "tools/mock/vectors/S1/S1.DOORBELL.$d.001.dat" \
            "S1.DOORBELL.$d.001" S1 "$d" S1.DOORBELL \
            "the schema fixture of the self test" n/a none - "self test fixture" \
            >> "$dir/vectors/manifest.tsv"
    done
    {
        printf 'seg\tanchor_id\tboundary\tin_shape\tout_expected\ttruth_source\troute\tinput_vec\texpected_vec\thost_assert\thost_fmt\tboard_fmt\tconsumer\tstatus\n'
        printf 'S1\tS1.DOORBELL\tfixture\tfixture\tfixture\tfixture\tn/a\ttools/mock/vectors/S1/S1.DOORBELL.in.001.dat\ttools/mock/vectors/S1/S1.DOORBELL.exp.001.dat\tfixture\t<prefix>S1.DOORBELL ok=%%u\t<prefix>S1.DOORBELL ok=%%u\tfixture\tregistered\n'
    } > "$dir/segment-map.tsv"
}

# The mini factory must be green first: a negative that is red anyway would prove
# nothing about the break it claims to catch.
vecwant() {
    name=$1
    want=$2
    tree=$3
    if sh "$mock/check-vectors.sh" --vectors-root "$tree/vectors" \
            --segment-map "$tree/segment-map.tsv" > "$vecfixtures/$name.out" 2>&1; then
        rc=0
    else
        rc=1
    fi
    if [ "$want" = "pass" ]; then
        if [ "$rc" = "0" ]; then
            vecok=$((vecok + 1))
        else
            vecbad=$((vecbad + 1))
            echo "  vector fixture $name: expected a clean pass, the validator reported:" >&2
            sed 's/^/    /' "$vecfixtures/$name.out" >&2
        fi
        return
    fi
    # A must-go-red fixture: the exit code and the rule id must both be there.
    if [ "$rc" = "0" ]; then
        vecbad=$((vecbad + 1))
        echo "  vector fixture $name: the validator accepted a tree it must refuse" >&2
        return
    fi
    if grep -q "^$want " "$vecfixtures/$name.out"; then
        vecok=$((vecok + 1))
    else
        vecbad=$((vecbad + 1))
        echo "  vector fixture $name: no $want finding in the validator output" >&2
        sed 's/^/    /' "$vecfixtures/$name.out" >&2
    fi
}

vecok=0
vecbad=0

write_mini_factory "$vecfixtures/green"
vecwant green pass "$vecfixtures/green"

# a tampered checksum: one payload byte changed, the header left alone
rm -rf "$vecfixtures/tamper"; cp -r "$vecfixtures/green" "$vecfixtures/tamper"
sed 's/^00ff01$/00ff02/' "$vecfixtures/green/vectors/S1/S1.DOORBELL.in.001.dat" \
    > "$vecfixtures/tamper/vectors/S1/S1.DOORBELL.in.001.dat"
vecwant tamper crc32 "$vecfixtures/tamper"

# a fake segment id, in a header and in a manifest row
rm -rf "$vecfixtures/fake-seg"; cp -r "$vecfixtures/green" "$vecfixtures/fake-seg"
sed 's/^seg=S1$/seg=S5/' "$vecfixtures/fake-seg/vectors/S1/S1.DOORBELL.in.001.dat" \
    > "$vecfixtures/fake-seg/vectors/S1/S1.DOORBELL.in.001.dat.tmp"
mv "$vecfixtures/fake-seg/vectors/S1/S1.DOORBELL.in.001.dat.tmp" \
   "$vecfixtures/fake-seg/vectors/S1/S1.DOORBELL.in.001.dat"
vecwant fake_seg seg "$vecfixtures/fake-seg"

# an empty truth tag
rm -rf "$vecfixtures/empty-truth"; cp -r "$vecfixtures/green" "$vecfixtures/empty-truth"
sed 's/^truth=.*$/truth=/' "$vecfixtures/empty-truth/vectors/S1/S1.DOORBELL.in.001.dat" \
    > "$vecfixtures/empty-truth/vectors/S1/S1.DOORBELL.in.001.dat.tmp"
mv "$vecfixtures/empty-truth/vectors/S1/S1.DOORBELL.in.001.dat.tmp" \
   "$vecfixtures/empty-truth/vectors/S1/S1.DOORBELL.in.001.dat"
vecwant empty_truth truth "$vecfixtures/empty-truth"

# a manifest row whose file does not exist
rm -rf "$vecfixtures/manifest"; cp -r "$vecfixtures/green" "$vecfixtures/manifest"
printf 'tools/mock/vectors/S1/no-such-vector.dat\tS1.NOPE.in.001\tS1\tin\tS1.NOPE\tx\tn/a\tnone\t-\tself test fixture\n' \
    >> "$vecfixtures/manifest/vectors/manifest.tsv"
vecwant manifest_mismatch manifest "$vecfixtures/manifest"

# a committed vector without a manifest row
rm -rf "$vecfixtures/unlisted"; cp -r "$vecfixtures/green" "$vecfixtures/unlisted"
head -2 "$vecfixtures/green/vectors/manifest.tsv" > "$vecfixtures/unlisted/vectors/manifest.tsv"
vecwant unlisted manifest "$vecfixtures/unlisted"

# a segment-map expected_vec pointing at a file that does not exist
rm -rf "$vecfixtures/mapcell"; cp -r "$vecfixtures/green" "$vecfixtures/mapcell"
sed 's#S1.DOORBELL.exp.001.dat#S1.DOORBELL.exp.404.dat#' "$vecfixtures/mapcell/segment-map.tsv" \
    > "$vecfixtures/mapcell/segment-map.tsv.tmp"
mv "$vecfixtures/mapcell/segment-map.tsv.tmp" "$vecfixtures/mapcell/segment-map.tsv"
vecwant segment_map_missing segment-map "$vecfixtures/mapcell"

if [ "$vecbad" = 0 ]; then
    result vector-negatives 1
else
    result vector-negatives 0
    echo "  vector-negatives: $vecok fixture(s) behaved, $vecbad did not" >&2
fi

echo "$suite: failures=$failures"
[ "$failures" = 0 ]
