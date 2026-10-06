#!/bin/sh
# Gate: keep this repository free of machine-local knowledge.
#
# freewebcam is an upstream-facing open-source project. Sources, docs and
# commit messages must read correctly to anyone who has never seen the
# development machine. Not allowed in the tree:
#   - absolute local paths (home/mnt/tmp), home-relative paths;
#   - lab or tooling identifiers, local archive/spec/issue references;
#   - any non-ASCII text outside README.md (README.md is bilingual on purpose).
#
# This script excludes itself from the scans: it necessarily spells out the
# very patterns it looks for. It also excludes the vendored upstream test tree
# (third-party/FreeRTOS-Kernel-tests): those bytes are copied verbatim under the
# zero-patch discipline, so their content cannot be rewritten to satisfy the
# authoring rules here - a provenance file next to the tree is their record
# instead. Everything this project authors stays fully scanned.
#
# Usage:
#   tools/gates/check-local-knowledge.sh                 # scan tracked files
#   tools/gates/check-local-knowledge.sh origin/master..HEAD
#                                                        # + commit messages
# Exit status: 0 clean, 1 findings.

set -eu

self=tools/gates/check-local-knowledge.sh
vendored=third-party/FreeRTOS-Kernel-tests
paths_self=':!'"$self"
paths_vendored=':!'"$vendored"

patterns='/(home|mnt|tmp)/|~/|KI-[0-9]|itx-wiki|oslab|backups/|web_cmera_|project_rtos_webcam|project_web_camera|\.agents/skills'

status=0

# $1 = label, reads findings from stdin
report() {
	if [ -n "$1" ]; then
		printf '%s\n' "$1"
		echo "check-local-knowledge: FAIL ($2)"
		status=1
	else
		echo "check-local-knowledge: PASS ($2)"
	fi
}

files=$(git ls-files -- "$paths_self" "$paths_vendored")

findings=$(printf '%s\n' "$files" | xargs -r grep -nHIE "$patterns" -- 2>/dev/null || true)
report "$findings" "local-path/identifier scan"

nonscii=$(printf '%s\n' "$files" | grep -v '^README\.md$' \
	| xargs -r grep -nHP '[^\x00-\x7F]' -- 2>/dev/null \
	| cut -d: -f1 | sort -u || true)
report "$nonscii" "ASCII-only scan (README.md excluded)"

if [ $# -ge 1 ]; then
	msgs=$(git log --format='%H%n%s%n%b' "$1" | grep -nE "$patterns" || true)
	report "$msgs" "commit-message scan ($1)"
fi

exit $status
