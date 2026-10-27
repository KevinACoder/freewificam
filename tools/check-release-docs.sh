#!/usr/bin/env bash
#
# check-release-docs.sh - the reader-facing documents must be readable by
# someone who has never seen the development machine.
#
# The scan covers the files a release puts in front of a reader: the README,
# the changelog, the version and licence files, docs/ and known_issues/.  It
# does not scan code comments or the imported trees; see docs/RELEASE.md for
# the boundary.
#
# Rejected in those files:
#   - non-ASCII text;
#   - machine-local paths (home directories, mounts, system state);
#   - private repository and bench names;
#   - internal round identifiers;
#   - references to records that do not live in this repository;
#   - internal issue/ledger identifiers.
#
# docs/COMMIT-CONVENTION.md is skipped: it necessarily spells out the terms it
# bans, exactly like tools/check-commit-msg.sh does.
#
# Usage:
#   tools/check-release-docs.sh          # scan the tracked files
#   tools/check-release-docs.sh FILE...  # scan the named files
#
# Exit: 0 clean, 1 findings, 2 usage.

set -u

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 2

skip='docs/COMMIT-CONVENTION.md'

if [ "$#" -gt 0 ]; then
	files="$*"
else
	# --others includes files that exist but are not staged yet: a release
	# document is checked before it is committed, not after.
	files=$(git ls-files --cached --others --exclude-standard -- \
		README.md CHANGELOG.md VERSION LICENSE LICENSES.md \
		'docs/*.md' 'known_issues/*.md' 2>/dev/null)
fi

# Each pattern is one claim about a reader-facing document.  The private record
# patterns deliberately require a date-shaped path, so a line that merely uses
# the words "run" or "decision" is not a finding.
patterns='(~/|/home/|/mnt/|/srv/|/var/lib/|/tmp/)'
patterns+='|(project_rtos_webcam|project_rea_wcam|rk3568_lab|oslab|itx-wiki)'
patterns+='|\b(L1|L2|L3|REL)-[0-9]{2}'
patterns+='|(runs/[0-9]{4}-|runs/[0-9]{8}|iterations/|decisions/|fw-trm)'
patterns+='|(KI-|GAP-20|99 #[0-9])'
patterns+='|\bGT\b|\bTRM\b'

fail=0
checked=0

for f in $files; do
	[ -f "$f" ] || continue
	[ "$f" = "$skip" ] && continue
	checked=$((checked + 1))

	if LC_ALL=C grep -qP '[^\x00-\x7F]' "$f"; then
		printf 'check-release-docs: FAIL %s: non-ASCII text\n' "$f" >&2
		fail=1
	fi

	hits=$(grep -nE "$patterns" "$f" | head -5)
	if [ -n "$hits" ]; then
		printf 'check-release-docs: FAIL %s: private reference\n%s\n' "$f" "$hits" >&2
		fail=1
	fi
done

if [ "$fail" -ne 0 ]; then
	exit 1
fi

printf 'check-release-docs: %d files clean\n' "$checked"
