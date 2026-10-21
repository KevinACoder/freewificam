#!/usr/bin/env bash
#
# install-hooks.sh - install the commit-msg gate for this repository.
#
set -eu

root="$(git rev-parse --show-toplevel)"
gitdir="$(git rev-parse --git-dir)"
case "$gitdir" in
	/*) ;;
	*) gitdir="$root/$gitdir" ;;
esac
hook="$gitdir/hooks/commit-msg"

checker="$root/tools/check-commit-msg.sh"
[ -x "$checker" ] || { echo "install-hooks: no executable $checker" >&2; exit 1; }

mkdir -p "$gitdir/hooks"
cat > "$hook" <<EOF
#!/bin/sh
exec "$checker" "\$1"
EOF
chmod +x "$hook"
echo "installed $hook -> $checker"
