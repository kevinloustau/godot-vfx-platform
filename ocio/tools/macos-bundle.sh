#!/usr/bin/env bash
# macOS bundling for the OCIO GDExtension.
#
#   stage <main-dylib> <dep-libdir> <out-dir>
#       Copy the built library plus every dependency into <out-dir>, making
#       the bundle relocatable: absolute install names are rewritten to
#       "@rpath/<name>", already-@rpath names (conda-forge style) are
#       resolved from <dep-libdir>. <dep-libdir> is the lib dir the library
#       was linked against (Homebrew prefix/lib or conda env/lib).
#       Run once per architecture.
#
#   merge <target> <arm64-dir> <x86_64-dir> <out-dir>
#       lipo-merge the two staged trees into universal binaries in <out-dir>,
#       ad-hoc codesign everything (required on arm64), and emit
#       dependencies-snippet.txt plus a ready-to-use ocio.gdextension
#       (the template + the macOS [dependencies] section).
#
# Usage: macos-bundle.sh <stage|merge> ...
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
# Per-project overrides (defaults: the ocio extension). Export LIB_PREFIX,
# ADDON_RES_BASE and GDEXTENSION_TEMPLATE to bundle another project.
gdextension_template=${GDEXTENSION_TEMPLATE:-"$script_dir/../ocio.gdextension"}
LIB_PREFIX=${LIB_PREFIX:-libgdocio}
ADDON_RES_BASE=${ADDON_RES_BASE:-res://addons/ocio}

fail() { echo "macos-bundle: $*" >&2; exit 1; }

# The LC_LOAD_DYLIB paths of a Mach-O file (one per line), excluding the
# file's own install name (LC_ID_DYLIB), which otool -L prints first.
refs_of() {
	local file=$1 ids ref
	ids=$(otool -D "$file" 2>/dev/null | tail -n +2 | tr -d '[:space:]')
	otool -L "$file" | awk '/^\t/ { print $1 }' | while read -r ref; do
		printf '%s\n' "$ids" | grep -Fqx "$ref" || printf '%s\n' "$ref"
	done
}

# All staged dylibs except the main library (libgdocio.*). Only .dylib
# files: the output dir also holds the descriptor and the snippet.
dep_names() {
	local f
	for f in *.dylib; do
		case "$f" in
		$LIB_PREFIX*) continue ;;
		esac
		[ -e "$f" ] && printf '%s\n' "$f"
	done | sort
}

stage() { # <main-dylib> <dep-libdir> <out-dir>
	local lib=$1 libdir=$2 out=$3 name
	[ -f "$lib" ] || fail "library not found: $lib"
	[ -d "$libdir" ] || fail "dep libdir not found: $libdir"
	mkdir -p "$out"
	name=$(basename "$lib")
	cp "$lib" "$out/$name"
	# Give the library an arch-agnostic install name: the shipped file is the
	# lipo-merged libgdocio.macos.<target>.universal.dylib.
	universal_name=$(echo "$name" | sed -E 's/\.(arm64|x86_64)\.dylib$/.universal.dylib/')
	install_name_tool -id "@rpath/$universal_name" "$out/$name"
	rewrite_tree "$libdir" "$out" "$name"
	echo "staged $name in $out"
}

# Depth-first: make the references of <out>/<name> relocatable and stage any
# newly discovered dependency.
rewrite_tree() { # <dep-libdir> <out-dir> <name>
	local libdir=$1 out=$2 name=$3 ref depname src
	for ref in $(refs_of "$out/$name"); do
		src=
		case "$ref" in
		@rpath/*) # already relocatable (conda-forge) — resolve the file
			depname=${ref#@rpath/}
			src=$libdir/$depname
			;;
		@loader_path/* | @executable_path/*) continue ;;
		/usr/lib/* | /System/Library/*) continue ;; # system libs stay as-is
		*) # absolute install name (Homebrew style) — rewrite it
			depname=$(basename "$ref")
			src=$ref
			install_name_tool -change "$ref" "@rpath/$depname" "$out/$name"
			;;
		esac
		if [ ! -f "$out/$depname" ]; then
			[ -f "$src" ] || fail "dependency missing: $src (needed by $name)"
			cp -L "$src" "$out/$depname"
			install_name_tool -id "@rpath/$depname" "$out/$depname"
			install_name_tool -add_rpath "@loader_path" "$out/$depname" 2>/dev/null || true
			rewrite_tree "$libdir" "$out" "$depname"
		fi
	done
}

merge() { # <target> <arm64-dir> <x86_64-dir> <out-dir>
	local target=$1 armdir=$2 x86dir=$3 out=$4
	local main_arm="$LIB_PREFIX.macos.$target.arm64.dylib"
	local main_x86="$LIB_PREFIX.macos.$target.x86_64.dylib"
	[ -f "$armdir/$main_arm" ] || fail "missing $armdir/$main_arm"
	[ -f "$x86dir/$main_x86" ] || fail "missing $x86dir/$main_x86"
	mkdir -p "$out"

	# The dependency sets must match across arches, otherwise the lipo pairing
	# below is wrong (e.g. different package versions in the two prefixes).
	local arm_deps x86_deps dep
	arm_deps=$(cd "$armdir" && dep_names)
	x86_deps=$(cd "$x86dir" && dep_names)
	if [ "$arm_deps" != "$x86_deps" ]; then
		fail "staged dependency sets differ between arches (pin the same
package versions in both dependency prefixes):
arm64:
$arm_deps
x86_64:
$x86_deps"
	fi

	# Main library -> universal binary.
	lipo -create -output "$out/$LIB_PREFIX.macos.$target.universal.dylib" \
		"$armdir/$main_arm" "$x86dir/$main_x86"

	# Dependencies -> universal binaries.
	for dep in $arm_deps; do
		lipo -create -output "$out/$dep" "$armdir/$dep" "$x86dir/$dep"
	done

	# Strip local symbols (smaller bundle) then ad-hoc codesign everything
	# (mandatory on arm64 after modifying binaries; strip must come first).
	local f
	for f in "$out"/*.dylib; do
		strip -x "$f" 2>/dev/null || true
	done
	for f in "$out"/*.dylib; do
		codesign --force --sign - "$f"
	done

	write_dist "$out"
}

write_dist() { # <out-dir> — dependencies snippet + ready-to-use .gdextension
	local out=$1 dep snippet="$out/dependencies-snippet.txt"
	{
		echo "; Generated by ocio/tools/macos-bundle.sh — macOS [dependencies]"
		echo "; section for ocio.gdextension (universal dylibs, @rpath install names,"
		echo "; copied to Contents/Frameworks by the macOS export)."
		printf 'macos.debug = {\n'
		for dep in $(cd "$out" && dep_names); do
			printf '\t"%s/macos/%s": "Contents/Frameworks",\n' "$ADDON_RES_BASE" "$dep"
		done
		printf '}\n'
		printf 'macos.release = {\n'
		for dep in $(cd "$out" && dep_names); do
			printf '\t"%s/macos/%s": "Contents/Frameworks",\n' "$ADDON_RES_BASE" "$dep"
		done
		printf '}\n'
	} > "$snippet"

	# Ready-to-use descriptor: template + generated macOS dependencies,
	# injected right after the marker comment in [dependencies].
	[ -f "$gdextension_template" ] || fail "template not found: $gdextension_template"
	awk -v snippet="$snippet" '
		{ print }
		/^; macos dependencies are generated/ && !done {
			while ((getline line < snippet) > 0) print line
			close(snippet)
			done = 1
		}
	' "$gdextension_template" > "$out/$gdext_name"
	# The template itself mentions macos.debug under [libraries], so check for
	# an injected snippet line instead.
	grep -q 'Contents/Frameworks' "$out/$gdext_name" ||
		fail "marker comment missing in $gdextension_template"
}

case "${1:-}" in
stage)
	[ $# -eq 4 ] || fail "usage: $0 stage <main-dylib> <dep-libdir> <out-dir>"
	stage "$2" "$3" "$4"
	;;
merge)
	[ $# -eq 5 ] || fail "usage: $0 merge <target> <arm64-dir> <x86_64-dir> <out-dir>"
	merge "$2" "$3" "$4" "$5"
	;;
*)
	fail "usage: $0 <stage|merge> ..."
	;;
esac
