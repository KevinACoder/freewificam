#!/bin/sh
# @file    gen-segment-map.sh
# @brief   Generate the segment header from the registry: one definition per anchor, so
#          host assertions and board prints cannot drift apart.
# @author  zhugengyu
# @date    07.10.2026
# @design builder - ref linux:init/main.c:1377
#
# The generated header is a build artifact: it is written under tools/mock/build/ and is
# never committed, because the registry is the single source of truth. Regenerating it
# must produce the same bytes, which the container checks on every run.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
out=$here/build/segment_map.h
mkdir -p "$here/build"

{
    echo "/* Generated from tools/mock/segment-map.tsv by gen-segment-map.sh. Do not edit. */"
    echo "#ifndef MOCK_SEGMENT_MAP_H"
    echo "#define MOCK_SEGMENT_MAP_H"
    echo
    awk -F'\t' '
    NR == 1 { next }
    {
        seg = $1; anchor = $2
        rows++
        if (anchor != "-") {
            macros[anchor] = 1
        }
        if (!(seg in seg_status)) {
            seg_status[seg] = $14
            seg_order[++segs] = seg
        }
    }
    END {
        printf "#define SEGMENT_MAP_ROWS %d\n", rows
        printf "#define SEGMENT_MAP_ANCHORS %d\n", length(macros)
        for (i = 1; i <= segs; i++) {
            printf "#define SEGMENT_%s_STATUS \"%s\"\n", seg_order[i], seg_status[seg_order[i]]
        }
    }
    ' "$here/segment-map.tsv"
    echo
    awk -F'\t' '
    function sanitize(s) {
        gsub(/\./, "_", s)
        return toupper(s)
    }
    NR == 1 { next }
    $2 != "-" {
        name = sanitize($2)
        printf "#define ANCHOR_%s_ID \"%s\"\n", name, $2
        printf "#define ANCHOR_%s_HOST_FMT \"%s\"\n", name, $11
        printf "#define ANCHOR_%s_BOARD_FMT \"%s\"\n", name, $12
        printf "#define ANCHOR_%s_CONSUMER \"%s\"\n", name, $13
        print ""
    }
    ' "$here/segment-map.tsv"
    echo "#endif /* MOCK_SEGMENT_MAP_H */"
} > "$out"

echo "gen-segment-map: wrote $out"
