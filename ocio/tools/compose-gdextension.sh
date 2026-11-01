#!/usr/bin/env bash
# Compose a multi-platform ocio.gdextension.
#
# Each platform bundle (dist/ocio/macos, dist/ocio/linux64, ...) ships a
# dependencies-snippet.txt produced by macos-bundle.sh / linux-bundle.sh.
# This injects any number of them into the descriptor template, so a Godot
# project that ships more than one platform gets a single descriptor with
# the windows + macos + linux [dependencies] sections.
#
# Usage:
#   compose-gdextension.sh <out.gdextension> <platform>:<bundle-dir> ...
#
#   compose-gdextension.sh addons/ocio/ocio.gdextension \
#     macos:dist/ocio/macos linux:dist/ocio/linux64 windows:dist/ocio/win64
#
# A windows bundle replaces the template's static win64 DLL list; without
# one, that list is kept as-is.
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
template="$script_dir/../ocio.gdextension"

fail() { echo "compose-gdextension: $*" >&2; exit 1; }

[ $# -ge 2 ] || fail "usage: $0 <out.gdextension> <platform>:<bundle-dir> ..."
out=$1
shift

macos_snippet=
linux_snippet=
windows_snippet=
for spec in "$@"; do
	plat=${spec%%:*}
	dir=${spec#*:}
	case "$plat" in
	macos | linux | windows) ;;
	*) fail "unknown platform '$plat' (macos|linux|windows)" ;;
	esac
	snip="$dir/dependencies-snippet.txt"
	[ -f "$snip" ] || fail "no dependencies-snippet.txt in $dir"
	case "$plat" in
	macos) macos_snippet=$snip ;;
	linux) linux_snippet=$snip ;;
	windows) windows_snippet=$snip ;;
	esac
done

[ -f "$template" ] || fail "template not found: $template"

awk -v macos="${macos_snippet:-}" -v linux="${linux_snippet:-}" \
	-v windows="${windows_snippet:-}" '
	# A supplied windows bundle replaces the template'\''s static win64 list.
	windows != "" && /^windows\.x86_64 = \{/ {
		while ((getline line < windows) > 0) print line
		close(windows)
		skipping = 1
		next
	}
	skipping { if ($0 == "}") skipping = 0; next }
	{ print }
	/^; macos dependencies are generated/ && macos != "" {
		while ((getline line < macos) > 0) print line
		close(macos); macos = ""
	}
	/^; linux dependencies are generated/ && linux != "" {
		while ((getline line < linux) > 0) print line
		close(linux); linux = ""
	}
' "$template" > "$out"

# Sanity: one [dependencies] key per requested platform section.
if [ -n "$macos_snippet" ]; then
	grep -q '^macos\.debug = {' "$out" || fail "macos section missing from $out"
fi
if [ -n "$linux_snippet" ]; then
	grep -q '^linux\.x86_64 = {' "$out" || fail "linux section missing from $out"
fi
if [ -n "$windows_snippet" ]; then
	grep -q '^windows\.x86_64 = {' "$out" || fail "windows section missing from $out"
fi
echo "wrote $out"
