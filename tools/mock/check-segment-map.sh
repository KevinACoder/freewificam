#!/bin/sh
# @file    check-segment-map.sh
# @brief   Schema check of the segment registry: the container refuses to run on a table
#          that cannot be trusted.
# @author  zhugengyu
# @date    07.10.2026
# @design registry - ref linux:drivers/base/dd.c:1
#
# Rules enforced here (the container treats a red as a configuration error, never as a
# test failure, and a misplaced row must never pass):
#   - the header names the fourteen fields, in order;
#   - every row has fourteen fields, and the fields that describe an anchor are not empty;
#   - segment and status come from fixed sets; a not-built row says so explicitly;
#   - an active row's anchor id is unique and well formed; rows without an anchor yet use
#     a single dash and may not claim to be active;
#   - the route field is either not applicable or a wireless route annotation;
#   - the two vector columns are a single dash or a path to an existing vector file (the
#     cross-check against the file's header belongs to check-vectors.sh);
#   - the two sides of an anchor print the same field-name sequence.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
# Vector cells carry repository-relative paths, so they resolve against the repository
# root - the same base gen-vector.sh writes.
root=$(cd "$here/../.." && pwd)
# The table can be overridden so the container's self test can run the same check over
# deliberately broken tables and prove it goes red.
map=${1:-$here/segment-map.tsv}

if [ ! -f "$map" ]; then
    echo "check-segment-map: $map is missing"
    exit 1
fi

expected='seg	anchor_id	boundary	in_shape	out_expected	truth_source	route	input_vec	expected_vec	host_assert	host_fmt	board_fmt	consumer	status'
actual=$(head -1 "$map")
if [ "$actual" != "$expected" ]; then
    echo "check-segment-map: header does not name the fourteen fields in order"
    exit 1
fi

awk -F'\t' -v root="$root" '
function names(s,   res) {
    res = ""
    while (match(s, /[A-Za-z_][A-Za-z0-9_]*=/)) {
        res = res " " substr(s, RSTART, RLENGTH - 1)
        s = substr(s, RSTART + RLENGTH)
    }
    return res
}
# A vector column is a single dash or a path to an existing vector file. Relative paths
# are repository-relative; the magic line is what makes the file a vector file.
function check_vec_column(no, name, cell,   path, first) {
    if (cell == "") {
        printf "row %d: %s is empty (use -)\n", no, name
        return 1
    }
    if (cell == "-") {
        return 0
    }
    path = (substr(cell, 1, 1) == "/") ? cell : root "/" cell
    if ((getline first < path) <= 0) {
        printf "row %d: %s %s is not a readable file\n", no, name, cell
        close(path)
        return 1
    }
    close(path)
    if (first != "mockvec1") {
        printf "row %d: %s %s is not a vector file\n", no, name, cell
        return 1
    }
    return 0
}
NR == 1 { next }
{
    rows++
    if (NF != 14) {
        printf "row %d: %d fields, want 14\n", NR, NF
        bad = 1
        next
    }
    seg = $1; anchor = $2; boundary = $3; in_shape = $4; out_expected = $5
    truth = $6; route = $7; input_vec = $8; expected_vec = $9
    host_assert = $10; host_fmt = $11; board_fmt = $12; consumer = $13; status = $14

    if (seg !~ /^(S1|S2|S3|S4|S5|M)$/) { printf "row %d: bad segment %s\n", NR, seg; bad = 1 }
    if (status !~ /^(active|registered|not-built)$/) { printf "row %d: bad status %s\n", NR, status; bad = 1 }

    if (boundary == "" || in_shape == "" || out_expected == "" || truth == "" ||
        host_assert == "" || host_fmt == "" || board_fmt == "" || consumer == "") {
        printf "row %d: an anchor field is empty\n", NR
        bad = 1
    }

    if (check_vec_column(NR, "input_vec", input_vec)) { bad = 1 }
    if (check_vec_column(NR, "expected_vec", expected_vec)) { bad = 1 }

    if (anchor == "-") {
        if (status == "active") { printf "row %d: active row without an anchor id\n", NR; bad = 1 }
    } else {
        # The registry examples (M.QUOTA, S1.EP0_SETUP) name an anchor as
        # <segment>.<name>, where the name may itself be compound with underscores.
        if (anchor !~ ("^" seg "\\.[A-Z0-9_]+$")) {
            printf "row %d: anchor id %s does not match the segment naming rule\n", NR, anchor
            bad = 1
        }
        if (seen[anchor]++) { printf "row %d: anchor id %s appears twice\n", NR, anchor; bad = 1 }
        if (index(host_fmt, anchor) == 0 || index(board_fmt, anchor) == 0) {
            printf "row %d: a print template does not carry the anchor id\n", NR
            bad = 1
        }
        if (names(host_fmt) != names(board_fmt)) {
            printf "row %d: the two sides do not print the same field names\n", NR
            bad = 1
        }
    }

    if (route == "") { printf "row %d: route is empty\n", NR; bad = 1 }
    else if (route != "n/a" && route !~ /^ieee80211-route:/) {
        printf "row %d: route must be n/a or an ieee80211-route annotation\n", NR
        bad = 1
    }

    if (seg == "S5" && status != "not-built") {
        printf "row %d: segment S5 has no consumer and must be not-built\n", NR
        bad = 1
    }
    if (status == "not-built" && consumer != "none") {
        printf "row %d: a not-built row must say its consumer is none\n", NR
        bad = 1
    }

    count[status]++
}
END {
    printf "segment-map: rows=%d active=%d registered=%d not-built=%d\n",
           rows, count["active"], count["registered"], count["not-built"]
    exit bad
}
' "$map"
