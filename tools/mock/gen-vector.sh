#!/bin/sh
# @file    gen-vector.sh
# @brief   The factory's single generator: a descriptor is turned into a vector data
#          file plus its manifest row, checksums included.
# @author  zhugengyu
# @date    07.10.2026
# @design builder - ref linux:init/main.c:1377
#
# Usage:
#   tools/mock/gen-vector.sh --from-spec tools/mock/vectors/collect/<vec>.spec
#   tools/mock/gen-vector.sh --list
#
# A descriptor is a small key=value file with the header fields that are not computed
# (seg/dir/seq/anchor/truth/source/captured/redact), the three manifest-only cells
# (truth_note/route/norm) and the consumer note, followed by the payload as lower-case
# hex in one or more "hex=" lines. payload_bytes, crc32 and sha256 are computed here,
# never written by hand; see the "Vector/Trace data files" chapter of tools/mock/README.md
# for the file format itself.
#
# The generated file name and the manifest row are derived from the descriptor: the
# vector id is <anchor>.<dir>.<NNN> (NNN is the sequence number, three digits), and the
# descriptor stem must equal it. Re-running the generator over an unchanged descriptor
# reproduces the same bytes and the same manifest row, so the factory stays diffable.
#
# Exit status: 0 written or listed; 2 usage error, malformed descriptor or missing
# registry (nothing is written when a descriptor is rejected).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
vecroot=$here/vectors
collect=$vecroot/collect
map=$here/segment-map.tsv
manifest=$vecroot/manifest.tsv
manifest_header='file	vec	seg	dir	anchor	truth	route	redact	norm	note'

usage() {
    echo "gen-vector: usage: $0 --from-spec <descriptor> | --list" >&2
    exit 2
}

mode=
spec=
while [ $# -gt 0 ]; do
    case $1 in
        --list)
            mode=list
            ;;
        --from-spec)
            mode=spec
            shift
            [ $# -gt 0 ] || usage
            spec=$1
            ;;
        *)
            usage
            ;;
    esac
    shift
done
[ -n "$mode" ] || usage

if [ "$mode" = list ]; then
    # One stem per line, in file-name order: the stems are the vector ids.
    [ -d "$collect" ] || exit 0
    for f in "$collect"/*.spec; do
        [ -f "$f" ] || continue
        base=${f##*/}
        echo "${base%.spec}"
    done
    exit 0
fi

if [ ! -f "$spec" ]; then
    echo "gen-vector: $spec: no such descriptor" >&2
    exit 2
fi
if [ ! -f "$map" ]; then
    echo "gen-vector: $map: the segment registry is missing" >&2
    exit 2
fi

stem=${spec##*/}
stem=${stem%.spec}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cat > "$tmp/parse.awk" <<'AWK'
# Descriptor parser and checksum engine. All validation errors are collected and
# reported before anything is written, so a rejected descriptor leaves no partial file.
function build_xor_table(   a, b, x, y, r, bit) {
    for (a = 0; a < 16; a++)
        for (b = 0; b < 16; b++) {
            r = 0; bit = 1; x = a; y = b
            while (x > 0 || y > 0) {
                if (x % 2 != y % 2) r += bit
                x = (x - x % 2) / 2; y = (y - y % 2) / 2; bit *= 2
            }
            xn[a, b] = r
        }
}
# Pure-arithmetic XOR of two 32-bit values (nibble table): POSIX awk has no bitwise
# built-ins, so the checksum stays computable on every host the container runs on.
function xor32(a, b,   r, place, aa, bb) {
    r = 0; place = 1
    while (a > 0 || b > 0) {
        aa = a % 16; bb = b % 16
        r += xn[aa, bb] * place
        a = (a - aa) / 16; b = (b - bb) / 16
        place *= 16
    }
    return r
}
function build_crc_table(   i, c, k) {
    for (i = 0; i < 256; i++) {
        c = i
        for (k = 0; k < 8; k++)
            c = (c % 2) ? xor32((c - c % 2) / 2, 3988292384) : (c - c % 2) / 2
        tbl[i] = c
    }
}
function hexval(c) {
    return index("0123456789abcdef", c) - 1
}
# CRC-32/ISO-HDLC over the decoded bytes, given as lower-case hex text.
function crc32_hex(hex,   i, n, b, crc) {
    crc = 4294967295
    n = length(hex)
    for (i = 1; i <= n; i += 2) {
        b = hexval(substr(hex, i, 1)) * 16 + hexval(substr(hex, i + 1, 1))
        crc = xor32((crc - crc % 256) / 256, tbl[xor32(crc % 256, b)])
    }
    return sprintf("%.8x", xor32(crc, 4294967295))
}
function bad(msg) {
    errs[++nbad] = msg
}
BEGIN {
    FS = "\t"

    build_xor_table()
    build_crc_table()
    # The published check value of the algorithm (CRC-32/ISO-HDLC of "123456789"):
    # an arithmetic mistake in the bitwise-free implementation cannot pass unnoticed.
    if (crc32_hex("313233343536373839") != "cbf43926")
        bad("internal error: the crc32 engine failed its check value")

    while ((getline line < mapfile) > 0) {
        n = split(line, f, FS)
        if (n >= 2 && f[2] != "" && f[2] != "anchor_id")
            anchor_seg[f[2]] = f[1]
    }
    close(mapfile)

    required["seg"] = 1; required["dir"] = 1; required["seq"] = 1
    required["anchor"] = 1; required["truth"] = 1; required["source"] = 1
    required["redact"] = 1; required["route"] = 1; required["norm"] = 1
    required["note"] = 1; required["truth_note"] = 1
    optional["captured"] = 1
}

{
    line = $0
    if (line == "" || substr(line, 1, 1) == "#")
        next
    lineno++
    if (in_hex) {
        if (line !~ /^hex=/) {
            bad("line " lineno ": the payload block is followed by " \
                "something other than a hex= line")
            next
        }
        h = substr(line, 5)
        if (h == "") {
            bad("line " lineno ": an empty hex= line")
            next
        }
        if (length(h) > 64) {
            bad("line " lineno ": a payload line carries more than 32 bytes")
            next
        }
        if (h !~ /^[0-9a-fA-F]+$/) {
            bad("line " lineno ": the payload is not lower-case hex")
            next
        }
        if (h !~ /^[0-9a-f]+$/) {
            bad("line " lineno ": the payload is hex but not lower-case")
            next
        }
        if (length(h) % 2 != 0) {
            bad("line " lineno ": a payload line has an odd number of hex digits")
            next
        }
        hex = hex h
        next
    }
    if (line !~ /^[a-z_]+=/) {
        bad("line " lineno ": expected a key=value line, got \"" line "\"")
        next
    }
    key = substr(line, 1, index(line, "=") - 1)
    value = substr(line, index(line, "=") + 1)
    if (key == "hex") {
        in_hex = 1
        # Re-enter the payload branch for this very line.
        h = value
        if (h == "" || length(h) > 64 || h !~ /^[0-9a-f]+$/ || length(h) % 2 != 0) {
            bad("line " lineno ": malformed hex= line")
            next
        }
        hex = hex h
        next
    }
    if (key in seen)
        bad("line " lineno ": the key " key " appears twice")
    if (!(key in required) && !(key in optional))
        bad("line " lineno ": unknown key " key)
    seen[key] = 1
    val[key] = value
}

END {
    if (nbad == 0) {
        n = split("seg dir seq anchor truth source redact route norm note truth_note", kk, " ")
        for (i = 1; i <= n; i++)
            if (!(kk[i] in val))
                bad("the required key " kk[i] " is missing")

        seg = val["seg"]; dir = val["dir"]; seq = val["seq"]; anchor = val["anchor"]
        truth = val["truth"]; source = val["source"]; redact = val["redact"]
        route = val["route"]; norm = val["norm"]; note = val["note"]
        truth_note = val["truth_note"]
        captured = ("captured" in val) ? val["captured"] : ""

        if (nbad == 0) {
            if (seg !~ /^(S1|S2|S3|S4|M)$/)
                bad("seg " seg " is not one of S1 S2 S3 S4 M")
            if (dir !~ /^(in|exp)$/)
                bad("dir must be in or exp (a record is not a generated vector)")
            if (seq !~ /^[0-9]+$/ || seq + 0 < 1)
                bad("seq must be an integer >= 1")
            else if (seq + 0 > 999)
                bad("seq must fit the three digits of the vector id")
            if (!(anchor in anchor_seg))
                bad("anchor " anchor " is not registered in the segment map")
            else if (anchor_seg[anchor] != seg)
                bad("anchor " anchor " belongs to segment " anchor_seg[anchor])
            if (truth == "")    bad("truth is empty")
            if (source == "")   bad("source is empty")
            if (redact == "")   bad("redact is empty (use none when no rule applied)")
            if (note == "")     bad("note is empty")
            if (truth_note == "") bad("truth_note is empty (the manifest records the full note)")
            if (route == "")
                bad("route is empty (use n/a for a non-wireless row)")
            else if (seg ~ /^S[34]$/) {
                if (route == "n/a")
                    bad("a wireless row needs a real route annotation, not n/a")
            } else if (route != "n/a") {
                bad("a non-wireless row must carry route=n/a")
            }
            if (norm != "-")
                bad("norm must be - for a generated vector")
            if (captured != "" && captured !~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/)
                bad("captured must be a YYYY-MM-DD date or empty for a derived vector")
            if (hex == "")
                bad("the payload is empty (a vector needs bytes)")

            vec = anchor "." dir "." sprintf("%03d", seq + 0)
            if (vec != specstem)
                bad("the descriptor stem " specstem " does not name the derived vector " \
                    "id " vec)
        }
    }

    if (nbad > 0) {
        for (i = 1; i <= nbad; i++)
            print "gen-vector: " specrel ": " errs[i]
        exit 2
    }

    payload_bytes = length(hex) / 2
    crc = crc32_hex(hex)

    print "mockvec1" > h1
    print "ver=1" > h1
    print "seg=" seg > h1
    print "dir=" dir > h1
    print "seq=" seq > h1
    print "anchor=" anchor > h1
    print "payload_bytes=" payload_bytes > h1
    print "crc32=" crc > h1
    close(h1)

    print "truth=" truth > h2
    print "source=" source > h2
    print "captured=" captured > h2
    print "redact=" redact > h2

    # Canonical payload: lower-case hex, 32 bytes (64 hex digits) per line.
    payload_line = ""
    for (i = 1; i <= length(hex); i += 2) {
        payload_line = payload_line substr(hex, i, 2)
        if (length(payload_line) == 64) {
            print payload_line > h2
            payload_line = ""
        }
    }
    if (payload_line != "")
        print payload_line > h2
    close(h2)

    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
           vec, seg, dir, anchor, truth_note, route, redact, norm, note > mrow
    close(mrow)

    # The decoded payload, for the independent sha256 pass in the wrapper.
    for (i = 1; i <= length(hex); i += 2)
        printf "%c", hexval(substr(hex, i, 1)) * 16 + hexval(substr(hex, i + 1, 1)) > payload
    close(payload)
}
AWK

if ! out=$(LC_ALL=C awk -v mapfile="$map" -v specstem="$stem" -v specrel="$spec" \
        -v h1="$tmp/h1" -v h2="$tmp/h2" -v mrow="$tmp/mrow.tsv" \
        -v payload="$tmp/payload.bin" -f "$tmp/parse.awk" "$spec" 2>&1); then
    printf '%s\n' "$out" >&2
    exit 2
fi
if [ -n "$out" ]; then
    printf '%s\n' "$out" >&2
    exit 2
fi

sha=$(sha256sum "$tmp/payload.bin" | cut -d' ' -f1)
case $sha in
    ????????????????????????????????????????????????????????????????) ;;
    *) echo "gen-vector: $spec: sha256sum did not produce a digest" >&2; exit 2 ;;
esac

IFS='	' read -r vec seg dir anchor truth_note route redact norm note < "$tmp/mrow.tsv"
payload_bytes=$(sed -n 's/^payload_bytes=//p' "$tmp/h1")
decoded_bytes=$(wc -c < "$tmp/payload.bin" | tr -d ' ')
# The decoded length must agree with the header count: a locale-aware byte emission
# would turn one high byte into a multi-byte sequence and inflate the file here.
if [ "$decoded_bytes" != "$payload_bytes" ]; then
    echo "gen-vector: $spec: the decoded payload is $decoded_bytes bytes, header says $payload_bytes" >&2
    exit 2
fi

rel=${vecroot#"$root"/}
mkdir -p "$vecroot/$seg"
target=$vecroot/$seg/$vec.dat
{
    cat "$tmp/h1"
    printf 'sha256=%s\n' "$sha"
    cat "$tmp/h2"
} > "$target"

# The manifest is rewritten in file-name order with the row replaced, so re-running the
# generator is idempotent and a hand edit can never leave two rows for one vector.
row=$rel/$seg/$vec.dat
if [ -f "$manifest" ]; then
    head=$(head -1 "$manifest")
    if [ "$head" != "$manifest_header" ]; then
        echo "gen-vector: $manifest: header does not name the ten fields in order" >&2
        exit 2
    fi
else
    printf '%s\n' "$manifest_header" > "$manifest"
fi
{
    tail -n +2 "$manifest" \
        | awk -F'\t' -v f="$row" -v v="$vec" 'NF > 0 && $1 != f && $2 != v'
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
           "$row" "$vec" "$seg" "$dir" "$anchor" "$truth_note" "$route" "$redact" "$norm" "$note"
} | LC_ALL=C sort > "$tmp/manifest-rows.tsv"
{
    printf '%s\n' "$manifest_header"
    cat "$tmp/manifest-rows.tsv"
} > "$tmp/manifest.tsv"
mv "$tmp/manifest.tsv" "$manifest"

echo "gen-vector: wrote $row"
