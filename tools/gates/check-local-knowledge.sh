#!/bin/sh
# Gate: keep this repository free of machine-local knowledge.
#
# freewificam is an upstream-facing open-source project. Sources, docs and
# commit messages must read correctly to anyone who has never seen the
# development machine. Not allowed in the tree:
#   - absolute local paths (home/mnt/tmp), home-relative paths;
#   - lab or tooling identifiers, local archive/spec/issue references, and
#     development-ledger paths (a spec, issue or run directory reference is
#     meaningless outside the development workspace; a URL that merely contains
#     one of these words is not matched);
#   - any non-ASCII text outside the README pair (README.md is the English text,
#     README_zh.md its Chinese translation; both are deliberately non-ASCII-capable).
#
# This script excludes itself from the scans: it necessarily spells out the
# very patterns it looks for. It also excludes the vendored upstream test tree
# (third-party/FreeRTOS-Kernel-tests) and the imported unit trees under
# components/ (net80211, skbuff, linuxkpi, cherrysh): those bytes are copied verbatim under
# the zero-patch discipline, so their content cannot be rewritten to satisfy the
# authoring rules here - an in-tree provenance file is their record instead. The
# project-authored records inside those trees (PROVENANCE.md, DIVERGENCES.md,
# MANIFEST.sha256) stay scanned, and everything else this project authors stays
# fully scanned.
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

# Imported unit trees: their bytes are excluded from both scans, their in-tree
# project-authored records are kept.
imported_units='components/net80211 components/skbuff components/linuxkpi components/cherrysh components/tlsf components/printf components/ulog components/fsl_sdmmc components/fatfs'
paths_imported=''
for unit in $imported_units; do
	paths_imported="$paths_imported :!$unit"
done
imported_records='components/net80211/PROVENANCE.md
components/net80211/DIVERGENCES.md
components/net80211/MANIFEST.sha256
components/skbuff/PROVENANCE.md
components/skbuff/DIVERGENCES.md
components/linuxkpi/PROVENANCE.md
components/linuxkpi/DIVERGENCES.md
components/cherrysh/PROVENANCE.md
components/cherrysh/DIVERGENCES.md
components/cherrysh/MANIFEST.sha256
components/tlsf/PROVENANCE.md
components/tlsf/DIVERGENCES.md
components/tlsf/MANIFEST.sha256
components/printf/PROVENANCE.md
components/printf/DIVERGENCES.md
components/printf/MANIFEST.sha256
components/ulog/PROVENANCE.md
components/ulog/DIVERGENCES.md
components/ulog/MANIFEST.sha256
components/fsl_sdmmc/PROVENANCE.md
components/fsl_sdmmc/MANIFEST.sha256
components/fatfs/PROVENANCE.md
components/fatfs/MANIFEST.sha256'

patterns='/(home|mnt|tmp)/|~/|KI-[0-9]|itx-wiki|oslab|backups/|web_cmera_|project_rtos_webcam|project_web_camera|\.agents/skills|(^|[^A-Za-z0-9_./-])(issues|specs|runs)/[A-Za-z0-9]'

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

# First-party files that live inside an imported unit's tree.  A unit's vendored
# bytes are excluded above (they are a verbatim copy of someone else's tree), but a
# file this project wrote underneath it is ours to keep clean, so the paths are named
# here and unioned into the scan set.  Add a path when a new first-party file is
# written under such a unit.
#
# The compatibility face is named as a whole directory rather than file by file: every
# header under components/linuxkpi/include is written by this project (it is the face the
# vendored drivers compile against, not an import), and enumerating them meant a header
# added by a later round was silently unscanned.  The remaining first-party files of the
# unit are named individually.
authored_in_units='components/linuxkpi/include
components/linuxkpi/fw_registry.h
components/linuxkpi/fw_registry.c
components/linuxkpi/fw/MANIFEST.sha256
components/linuxkpi/fw/rtl8188eufw_blob.c
components/linuxkpi/fw/rtw8821c_fw_blob.c
components/linuxkpi/fw/rtw8851b_fw_blob.c
components/linuxkpi/linuxkpi_impl.h
components/linuxkpi/linuxkpi_core.c
components/linuxkpi/linuxkpi_core.h
components/linuxkpi/linuxkpi_80211.c
components/linuxkpi/linuxkpi_80211.h
components/linuxkpi/linuxkpi_80211_helpers.c
components/linuxkpi/linuxkpi_cfg80211.c
components/linuxkpi/linuxkpi_cfg80211.h
components/linuxkpi/linuxkpi_compat.c
components/linuxkpi/linuxkpi_netdev.c
components/linuxkpi/linuxkpi_osdep.c
components/linuxkpi/lkpi_census.c
components/linuxkpi/lkpi_census.h
components/linuxkpi/lkpi_rxtrace.c
components/linuxkpi/lkpi_rxtrace.h
components/linuxkpi/linuxkpi_txq.c
components/linuxkpi/linuxkpi_txq.h
components/linuxkpi/linuxkpi_wiphy_work.c
components/linuxkpi/linuxkpi_wiphy_work.h
components/linuxkpi/skbuff/skbuff_pool.c
components/linuxkpi/skbuff/PROVENANCE.md
components/linuxkpi/skbuff/DIVERGENCES.md
components/linuxkpi/sdio/lkpi_sdio_backend.h
components/linuxkpi/sdio/lkpi_sdio_be.c
components/linuxkpi/sdio/linuxkpi_sdio.c
components/linuxkpi/sdio/linuxkpi_sdio.h
components/linuxkpi/usb/linuxkpi_usb.c
components/linuxkpi/usb/linuxkpi_usb.h
components/linuxkpi/usb/lkpi_usbh_class.c
components/linuxkpi/usb/lkpi_usbh_modeswitch.c
components/linuxkpi/usb/lkpi_usbh_modeswitch.h
components/linuxkpi/pci
components/linuxkpi/fw/iwl7260fw_blob.c
components/linuxkpi/linuxkpi_platform_target.c'

files=$(git ls-files -- "$paths_self" "$paths_vendored" $paths_imported)
records=$(git ls-files -- $imported_records)
authored=$(git ls-files -- $authored_in_units)
files=$(printf '%s\n%s\n%s\n' "$files" "$records" "$authored" | grep -v '^$' | sort -u)

findings=$(printf '%s\n' "$files" | xargs -r grep -nHIE "$patterns" -- 2>/dev/null || true)
report "$findings" "local-path/identifier scan"

nonscii=$(printf '%s\n' "$files" | grep -vE '^README(_zh)?\.md$' \
	| xargs -r grep -nHP '[^\x00-\x7F]' -- 2>/dev/null \
	| cut -d: -f1 | sort -u || true)
report "$nonscii" "ASCII-only scan (README.md / README_zh.md excluded)"

if [ $# -ge 1 ]; then
	msgs=$(git log --format='%H%n%s%n%b' "$1" | grep -nE "$patterns" || true)
	report "$msgs" "commit-message scan ($1)"
fi

exit $status
