#!/bin/sh
# @file    check-vectors.sh
# @brief   The factory's single validator: every vector file, the manifest and the vector
#          columns of the segment registry are cross-checked here, and nowhere else.
# @author  zhugengyu
# @date    07.10.2026
# @design checker - ref linux:scripts/checkpatch.pl:1
#
# Usage:
#   tools/mock/check-vectors.sh
#   tools/mock/check-vectors.sh --vectors-root <dir> --segment-map <file>
#
# Findings are one machine-readable line each, "<rule> <file> <field>: <detail>", and the
# last line is always "VECTORS-RESULT pass|fail". The exit status is 0 when every check
# passed; an empty vector set is a clean pass (the manifest and the registry are still
# validated when they exist), and a red exit is non-zero.
#
# Checks, by rule id:
#   magic          the first line is the mockvec1 token
#   header         the twelve key=value lines follow the fixed order, once each
#   ver            ver=1
#   seg            one of S1 S2 S3 S4 M (S5 is not built), matching the file's directory
#   dir            in, exp or trace
#   seq            an integer in 1..999; unique and contiguous per (anchor, dir)
#   anchor         registered in the segment map, in the segment the header claims
#   vec            the file stem is the derived vector id <anchor>.<dir>.<NNN>
#   payload        lower-case hex, an even number of digits, at most 32 bytes per line
#   payload_bytes  the declared count equals the decoded length
#   crc32          CRC-32/ISO-HDLC over the decoded bytes equals the header value
#   sha256         SHA-256 over the decoded bytes equals the header value
#   truth          non-empty; n/a exactly for a record, a real tag for a vector
#   source         non-empty; host:<suite>/<case> or board:<image>/<cmd> for a record
#   redact         non-empty (none when no rule applied)
#   captured       a YYYY-MM-DD date, or empty for a derived vector
#   manifest       header and ten columns; every row's values match the file it names;
#                  unique ids; one row per committed vector (records are exempt);
#                  rows in file-name order
#   route          a wireless row carries a real route annotation, every other row n/a
#   segment-map    a non-empty input_vec/expected_vec cell names an existing vector file
#                  whose segment, anchor, direction and vector id match the row

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

vectors_root=$here/vectors
map=$here/segment-map.tsv

while [ $# -gt 0 ]; do
    case $1 in
        --vectors-root)
            shift
            [ $# -gt 0 ] || { echo "check-vectors: --vectors-root needs a directory" >&2; exit 2; }
            vectors_root=$1
            ;;
        --segment-map)
            shift
            [ $# -gt 0 ] || { echo "check-vectors: --segment-map needs a file" >&2; exit 2; }
            map=$1
            ;;
        *)
            echo "check-vectors: usage: $0 [--vectors-root <dir>] [--segment-map <file>]" >&2
            exit 2
            ;;
    esac
    shift
done

if [ -d "$vectors_root" ]; then
    vectors_root=$(cd "$vectors_root" && pwd)
fi
if [ -f "$map" ]; then
    map=$(cd "$(dirname "$map")" && pwd)/$(basename "$map")
fi
manifest=$vectors_root/manifest.tsv

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

facts=$tmp/facts.tsv
findings=$tmp/findings.txt
dirs=$tmp/dirs.tsv
: > "$facts"
: > "$findings"
: > "$dirs"

# Every committed vector file, in file-name order; dir=trace records are exempt from the
# manifest but validated like any other file.
: > "$tmp/files.txt"
for f in "$vectors_root"/*/*.dat; do
    [ -f "$f" ] || continue
    printf '%s\n' "$f" >> "$tmp/files.txt"
done
LC_ALL=C sort -o "$tmp/files.txt" "$tmp/files.txt"

cat > "$tmp/vectors.awk" <<'AWK'
# One awk program in three modes (scan, manifest, segmentmap). The format's parser lives
# here once; the later modes read the facts the scan mode produced, so no second reading
# of the format can drift from the first.
function fail(rule, file, field, detail) {
    printf "%s %s %s: %s\n", rule, file, field, detail
}
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
function crc32_hex(hex,   i, n, b, crc) {
    crc = 4294967295
    n = length(hex)
    for (i = 1; i <= n; i += 2) {
        b = hexval(substr(hex, i, 1)) * 16 + hexval(substr(hex, i + 1, 1))
        crc = xor32((crc - crc % 256) / 256, tbl[xor32(crc % 256, b)])
    }
    return sprintf("%.8x", xor32(crc, 4294967295))
}
function relpath(p) {
    if (substr(p, 1, length(rootpfx)) == rootpfx)
        return substr(p, length(rootpfx) + 1)
    return p
}
# A file has one key: its path relative to the vectors root ("S1/<vec>.dat"). The manifest
# and the segment registry name files with the canonical repository-relative path
# (tools/mock/vectors/<seg>/<vec>.dat); a bare path relative to the vectors root and an
# absolute path are accepted too. The second form matters to the container's self test,
# which runs this very code over a mini factory tree under a temporary directory.
function resolve(cell) {
    if (substr(cell, 1, length(canonpfx) + 1) == canonpfx "/")
        return vectorsdir "/" substr(cell, length(canonpfx) + 2)
    if (substr(cell, 1, 1) == "/")
        return cell
    return vectorsdir "/" cell
}
function ckey(cell,   p) {
    p = resolve(cell)
    if (substr(p, 1, length(vectorsdir) + 1) == vectorsdir "/")
        return substr(p, length(vectorsdir) + 2)
    return p
}
function fkey(path) {
    if (substr(path, 1, length(vectorsdir) + 1) == vectorsdir "/")
        return substr(path, length(vectorsdir) + 2)
    return path
}
function fpath(key) {
    return vectorsdir "/" key
}
# The attributable name of a scanned file, for the finding lines: its path from the
# repository root when it is inside the repository, absolute otherwise.
function disp(key) {
    return relpath(fpath(key))
}
function basename(p,   m) {
    m = p
    sub(/^.*\//, "", m)
    return m
}
function dirname(p,   m) {
    m = p
    sub(/\/[^/]*$/, "", m)
    return basename(m)
}
function valid_date(s) {
    return s ~ /^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/
}

# --- mode scan: validate one file, print its findings, return its facts -------------
# key is the vectors-root-relative path of the file; the messages name it canonically.
# Returns "" when the file cannot be described (findings are printed regardless).
function scan(path, key,   line, n, k, value, nkeys, expected, hex, got, m, rel,
              seg, dir, seq, anchor, payload_bytes, crc, sha, truth, source,
              captured, redact, vec, pfile) {
    rel = disp(key)
    nkeys = 1                      # 1 = the magic token is the item yet to be seen
    hex = ""
    seg = ""; dir = ""; seq = ""; anchor = ""; vec = ""
    while ((getline line < path) > 0) {
        if (line == "" || substr(line, 1, 1) == "#")
            continue
        if (nkeys == 1) {
            if (line != "mockvec1") {
                fail("magic", rel, "mockvec", "the first line is not the mockvec1 token")
                close(path)
                return ""
            }
            nkeys = 2
            continue
        }
        if (line !~ /^[a-z][a-z0-9_]*=/) {
            if (nkeys <= 13) {
                fail("header", rel, "line", "expected a key=value line, got \"" line "\"")
                close(path)
                return ""
            }
            if (line !~ /^[0-9a-fA-F]+$/) {
                fail("payload", rel, "payload", "a payload line is not hex")
                continue
            }
            if (line !~ /^[0-9a-f]+$/) {
                fail("payload", rel, "payload", "a payload line is not lower-case hex")
                continue
            }
            if (length(line) % 2 != 0) {
                fail("payload", rel, "payload", "a payload line has an odd number of digits")
                continue
            }
            if (length(line) > 64) {
                fail("payload", rel, "payload", "a payload line carries more than 32 bytes")
                continue
            }
            hex = hex line
            continue
        }
        if (nkeys > 13) {
            fail("header", rel, "line", "a key=value line follows the payload block")
            close(path)
            return ""
        }
        key = substr(line, 1, index(line, "=") - 1)
        value = substr(line, index(line, "=") + 1)
        expected = order[nkeys - 1]
        if (key != expected) {
            fail("header", rel, key, "expected " expected " at this position")
            close(path)
            return ""
        }
        nkeys++
        if      (key == "ver")           ver = value
        else if (key == "seg")           seg = value
        else if (key == "dir")           dir = value
        else if (key == "seq")           seq = value
        else if (key == "anchor")        anchor = value
        else if (key == "payload_bytes") payload_bytes = value
        else if (key == "crc32")         crc = value
        else if (key == "sha256")        sha = value
        else if (key == "truth")         truth = value
        else if (key == "source")        source = value
        else if (key == "captured")      captured = value
        else if (key == "redact")        redact = value
    }
    close(path)

    if (nkeys <= 2 && hex == "") {
        fail("magic", rel, "mockvec", "the file carries no header")
        return ""
    }
    if (nkeys < 14) {
        for (m = nkeys - 1; m <= 12; m++)
            fail("header", rel, order[m], "the key is missing")
        return ""
    }

    if (ver != "1")
        fail("ver", rel, "ver", "ver=" ver " is not 1")
    if (seg !~ /^(S1|S2|S3|S4|M)$/)
        fail("seg", rel, "seg", seg " is not one of S1 S2 S3 S4 M")
    if (dir !~ /^(in|exp|trace)$/)
        fail("dir", rel, "dir", dir " is not in, exp or trace")
    if (seq !~ /^[0-9]+$/ || seq + 0 < 1 || seq + 0 > 999)
        fail("seq", rel, "seq", seq " is not an integer in 1..999")
    if (anchor == "")
        fail("anchor", rel, "anchor", "the anchor is empty")
    else if (!(anchor in anchor_seg))
        fail("anchor", rel, "anchor", anchor " is not registered in the segment map")
    else if (seg != anchor_seg[anchor])
        fail("anchor", rel, "anchor", anchor " is registered in segment " \
             anchor_seg[anchor] ", the header says " seg)

    if (seg != "" && dirname(rel) != seg)
        fail("seg", rel, "seg", "the file lives under " dirname(rel) ", the header says " seg)

    if (truth == "")   fail("truth", rel, "truth", "the truth tag is empty")
    if (source == "")  fail("source", rel, "source", "the source is empty")
    if (redact == "")  fail("redact", rel, "redact", "the redaction field is empty (use none)")
    if (captured != "" && !valid_date(captured))
        fail("captured", rel, "captured", "captured is neither empty nor a YYYY-MM-DD date")
    if (dir == "trace") {
        if (truth != "n/a")
            fail("truth", rel, "truth", "a record carries truth=n/a")
        if (source !~ /^(host|board):/)
            fail("source", rel, "source", "a record's source is host:<suite>/<case> or board:<image>/<cmd>")
    } else if (dir == "in" || dir == "exp") {
        if (truth == "n/a")
            fail("truth", rel, "truth", "a vector needs a real truth tag, not n/a")
    }

    if (anchor != "" && dir ~ /^(in|exp|trace)$/ && seq ~ /^[0-9]+$/ && seq + 0 <= 999) {
        vec = anchor "." dir "." sprintf("%03d", seq + 0)
        if (basename(rel) != vec ".dat")
            fail("vec", rel, "vec", "the file stem is not the derived vector id " vec)
    }

    if (hex == "")
        fail("payload_bytes", rel, "payload_bytes", "the file carries no payload")
    else {
        if (payload_bytes != length(hex) / 2)
            fail("payload_bytes", rel, "payload_bytes", "the header says " payload_bytes \
                 ", the decoded payload is " length(hex) / 2)
        got = crc32_hex(hex)
        if (crc != got)
            fail("crc32", rel, "crc32", "computed " got ", the header says " crc)
        pfile = payload_prefix "." ++pcount ".bin"
        for (m = 1; m <= length(hex); m += 2)
            printf "%c", hexval(substr(hex, m, 1)) * 16 + hexval(substr(hex, m + 1, 1)) > pfile
        close(pfile)
    }

    if (seg == "" || dir == "" || anchor == "" || seq == "" || vec == "")
        return ""
    return rel "\t" vec "\t" seg "\t" dir "\t" anchor "\t" seq "\t" payload_bytes "\t" \
           crc "\t" sha "\t" truth "\t" source "\t" redact "\t" pfile
}

# --- light header read, for a file referenced by the registry ------------------------
# Sets lh_exists; returns "seg\tdir\tanchor\tseq" or "".
function light_header(path,   line, key, value, nkeys, seg, dir, anchor, seq, m) {
    lh_exists = 0
    nkeys = 1
    seg = ""; dir = ""; anchor = ""; seq = ""
    while ((getline line < path) > 0) {
        lh_exists = 1
        if (line == "" || substr(line, 1, 1) == "#")
            continue
        if (line !~ /^[a-z_]+=/)
            continue
        key = substr(line, 1, index(line, "=") - 1)
        value = substr(line, index(line, "=") + 1)
        if (key == "seg")    seg = value
        if (key == "dir")    dir = value
        if (key == "anchor") anchor = value
        if (key == "seq")    seq = value
    }
    close(path)
    if (seg == "" && dir == "" && anchor == "")
        return ""
    return seg "\t" dir "\t" anchor "\t" seq
}

# lookup(cell): the facts of the file a cell names, keyed by the canonical path. A file
# that was scanned is served from the scan facts; a file outside the scanned set is read
# here (light header only), so a registry row can point at a file that is not committed
# under the vectors root.
function lookup(cell,   key, f) {
    key = ckey(cell)
    if (key == "" || !(key in f_seg)) {
        f = light_header(resolve(cell))
        if (!lh_exists) {
            f_missing[key] = 1
            return ""
        }
        n = split(f, a, "\t")
        f_seg[key] = a[1]; f_dir[key] = a[2]; f_anchor[key] = a[3]; f_seq[key] = a[4]
        f_vec[key] = ""
        if (a[1] != "" && a[2] != "" && a[3] != "" && a[4] ~ /^[0-9]+$/ && a[4] + 0 <= 999)
            f_vec[key] = a[3] "." a[2] "." sprintf("%03d", a[4] + 0)
    }
    return f_seg[key] "\t" f_dir[key] "\t" f_anchor[key] "\t" f_seq[key] "\t" f_vec[key]
}

BEGIN {
    order[1] = "ver"; order[2] = "seg"; order[3] = "dir"; order[4] = "seq"
    order[5] = "anchor"; order[6] = "payload_bytes"; order[7] = "crc32"
    order[8] = "sha256"; order[9] = "truth"; order[10] = "source"
    order[11] = "captured"; order[12] = "redact"

    while ((getline mline < mapfile) > 0) {
        m = split(mline, mf, "\t")
        if (m >= 2 && mf[2] != "" && mf[2] != "anchor_id")
            anchor_seg[mf[2]] = mf[1]
    }
    close(mapfile)

    # The file list is read in every mode: the manifest pass needs it for the
    # reverse-direction check even though only the scan pass parses the files. Files are
    # keyed by their vectors-root-relative path, the form the cells resolve to.
    while ((getline f < listfile) > 0) {
        if (f == "")
            continue
        flist[++nfiles] = f
        frel[nfiles] = fkey(f)
    }
    close(listfile)

    # What the scan pass found out about each file's direction (records are exempt from
    # the manifest, so the manifest pass must know which files are records).
    while ((getline d < dirfile) > 0) {
        if (d == "")
            continue
        n = split(d, dd, "\t")
        if (n >= 2)
            dir_of[dd[1]] = dd[2]
    }
    close(dirfile)

    if (mode == "scan") {
        build_xor_table()
        build_crc_table()
        if (crc32_hex("313233343536373839") != "cbf43926")
            fail("crc32", "-", "crc32", "internal error: the checksum engine failed its check value")
        for (i = 1; i <= nfiles; i++) {
            rel = frel[i]
            line = scan(flist[i], rel)
            if (line != "") {
                print line >> factfile
                close(factfile)
                p = split(line, ff, "\t")
                f_seg[rel] = ff[3]; f_dir[rel] = ff[4]; f_anchor[rel] = ff[5]
                f_seq[rel] = ff[6]; f_vec[rel] = ff[2]
                f_sha[rel] = ff[9]
                # The direction of every scanned file goes to a side file: the manifest
                # pass has to know which files are records, because records are exempt
                # from the manifest and parsing them twice would risk a second reading of
                # the format drifting from this one.
                print rel "\t" ff[4] >> dirfile
                close(dirfile)
                # Contiguity per (anchor, dir): distinct values in 1..count with the
                # largest equal to the count are exactly 1..count.
                g = ff[5] " " ff[4]
                cnt[g]++
                if (!(g in firstfile)) firstfile[g] = rel
                k = g " " ff[6]
                if (gseen[k]++) fail("seq", rel, "seq", "the sequence number " ff[6] \
                     " for " ff[5] "." ff[4] " appears twice")
                if (ff[6] + 0 > gmax[g]) gmax[g] = ff[6] + 0
                gname[g] = ff[5] "." ff[4]
            }
        }
        for (g in cnt)
            if (gmax[g] != cnt[g])
                fail("seq", firstfile[g], "seq", "the sequence numbers for " gname[g] \
                     " are not contiguous 1.." cnt[g])
        exit 0
    }

    if (mode == "manifest") {
        # No close() between the header read and the loop: under POSIX awk semantics a
        # close restarts the next read at line one, which would make the header count as
        # a data row. The status test is an explicit "<= 0" because getline returns -1
        # for an unreadable file, and -1 is a true value in awk.
        if ((getline mh < manifest) <= 0) {
            if (nfiles > 0)
                fail("manifest", relpath(manifest), "file", "the manifest is missing or empty")
            exit 0
        }
        if (mh != manifest_header)
            fail("manifest", relpath(manifest), "header", "the header does not name the ten fields in order")
        mrows = 0
        prev = ""
        while ((getline row < manifest) > 0) {
            mrows++
            if (row == "") {
                fail("manifest", relpath(manifest), "line " mrows, "an empty row")
                continue
            }
            n = split(row, r, "\t")
            if (n != 10) {
                fail("manifest", relpath(manifest), "line " mrows, n " columns, want 10")
                continue
            }
            file = r[1]; vec = r[2]; seg = r[3]; dir = r[4]; anchor = r[5]
            truth = r[6]; route = r[7]; redact = r[8]; norm = r[9]; note = r[10]
            if (file == "")   { fail("manifest", relpath(manifest), "file", "the file cell is empty"); continue }
            if (vec == "")    fail("manifest", file, "vec", "the vector id is empty")
            if (seg == "")    fail("manifest", file, "seg", "the segment is empty")
            if (dir == "")    fail("manifest", file, "dir", "the direction is empty")
            if (anchor == "") fail("manifest", file, "anchor", "the anchor is empty")
            if (truth == "")  fail("manifest", file, "truth", "the truth note is empty")
            if (route == "")  fail("manifest", file, "route", "the route is empty (use n/a)")
            if (redact == "") fail("manifest", file, "redact", "the redaction cell is empty")
            if (norm == "")   fail("manifest", file, "norm", "the normalisation cell is empty")
            if (note == "")   fail("manifest", file, "note", "the consumer note is empty")
            if (dir != "" && dir !~ /^(in|exp)$/)
                fail("manifest", file, "dir", dir " is not a vector direction (records are exempt from the manifest)")
            if (norm != "" && norm != "-")
                fail("manifest", file, "norm", "a vector row carries norm=-")
            if (route != "") {
                if (anchor ~ /^S[34]\./) {
                    if (route == "n/a")
                        fail("route", file, "route", "a wireless row needs a real route annotation")
                } else if (route != "n/a") {
                    fail("route", file, "route", "a non-wireless row says n/a")
                }
            }
            if (file != "" && (file in manseen))
                fail("manifest", file, "file", "the file appears twice in the manifest")
            manseen[file] = 1
            if (file != "")
                mankey[ckey(file)] = 1
            if (vec != "" && (vec in vecseen))
                fail("manifest", file, "vec", "the vector id appears twice in the manifest")
            vecseen[vec] = 1
            if (prev != "" && file < prev)
                fail("manifest", file, "file", "the rows are not in file-name order")
            prev = file

            facts = lookup(file)
            if (facts == "") {
                if (ckey(file) in f_missing)
                    fail("manifest", file, "file", "the file does not exist")
                else
                    fail("manifest", file, "file", "the file is not a valid vector")
                continue
            }
            split(facts, g, "\t")
            if (g[1] != seg)        fail("manifest", file, "seg", "the row says " seg \
                                            ", the header says " g[1])
            if (g[2] != dir)        fail("manifest", file, "dir", "the row says " dir \
                                            ", the header says " g[2])
            if (g[3] != anchor)     fail("manifest", file, "anchor", "the row says " anchor \
                                            ", the header says " g[3])
            if (g[5] != "" && g[5] != vec)
                fail("manifest", file, "vec", "the row says " vec ", the file derives " g[5])
        }
        close(manifest)

        # The other direction: every committed vector has a row (records are exempt).
        for (i = 1; i <= nfiles; i++) {
            rel = frel[i]
            if (dir_of[rel] == "trace")
                continue
            if (!(rel in mankey))
                fail("manifest", rel, "file", "no manifest row for this vector")
        }
        exit 0
    }

    if (mode == "segmentmap") {
        rowno = 0
        while ((getline mline < mapfile) > 0) {
            rowno++
            if (rowno == 1)
                continue
            n = split(mline, r, "\t")
            if (n < 9)
                continue
            anchor = r[2]; mseg = r[1]
            for (c = 8; c <= 9; c++) {
                cell = r[c]
                field = (c == 8) ? "input_vec" : "expected_vec"
                if (cell == "" || cell == "-")
                    continue
                key = ckey(cell)
                facts = lookup(cell)
                if (facts == "") {
                    if (key in f_missing)
                        fail("segment-map", cell, field, "the referenced file does not exist")
                    else
                        fail("segment-map", cell, field, "the referenced file is not a vector file")
                    continue
                }
                split(facts, g, "\t")
                if (g[1] != mseg)
                    fail("segment-map", cell, field, "the file's segment is " g[1] \
                         ", the row's segment is " mseg)
                if (anchor != "-" && g[3] != anchor)
                    fail("segment-map", cell, field, "the file's anchor is " g[3] \
                         ", the row's anchor is " anchor)
                if (field == "input_vec") {
                    if (g[2] != "in" && g[2] != "trace")
                        fail("segment-map", cell, field, "an input cell takes dir=in or dir=trace, the file says " g[2])
                } else if (g[2] != "exp") {
                    fail("segment-map", cell, field, "an expected cell takes dir=exp, the file says " g[2])
                }
                if (g[5] != "" && basename(key) != g[5] ".dat")
                    fail("segment-map", cell, field, "the file stem is not the vector id " g[5])
            }
        }
        close(mapfile)
        exit 0
    }
}
AWK

run_pass() {
    pass=$1
    log=$tmp/$pass.log
    LC_ALL=C awk -v mode="$pass" -v rootpfx="$root/" -v canonpfx="tools/mock/vectors" \
        -v vectorsdir="$vectors_root" -v mapfile="$map" \
        -v manifest="$manifest" -v manifest_header="$manifest_header" \
        -v listfile="$tmp/files.txt" -v factfile="$facts" -v dirfile="$dirs" \
        -v payload_prefix="$tmp/payload" -f "$tmp/vectors.awk" /dev/null > "$log" 2>&1 || true
    cat "$log" >> "$findings"
}

manifest_header='file	vec	seg	dir	anchor	truth	route	redact	norm	note'

run_pass scan

# The independent SHA-256 pass: the decoded payload of every scanned file, compared
# against the header by a tool other than the one that wrote it.
if [ -s "$facts" ]; then
    while IFS='	' read -r f vec seg dir anchor seq payload_bytes crc sha truth source redact pfile; do
        [ -n "$pfile" ] || continue
        got=$(sha256sum "$pfile" | cut -d' ' -f1)
        if [ "$sha" != "$got" ]; then
            printf 'sha256 %s sha256: computed %s, the header says %s\n' "$f" "$got" "$sha" \
                >> "$findings"
        fi
    done < "$facts"
fi

run_pass manifest
run_pass segmentmap

cat "$findings"
nfail=$(grep -c . "$findings" || true)

if [ "$nfail" = 0 ]; then
    printf 'VECTORS-RESULT pass\n'
else
    printf 'VECTORS-RESULT fail\n'
    exit 1
fi
