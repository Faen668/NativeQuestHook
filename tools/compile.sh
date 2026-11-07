#!/bin/sh
# Compiles the script mod with REDkit's own wcc_lite, into the mod's precompiled.rsblob.
#   sh tools/compile.sh [out folder]     the out folder defaults to build/scripts
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$HERE/build/scripts}"; mkdir -p "$OUT"
REDKIT="F:/Steam/steamapps/common/The Witcher 3 REDkit"
SCRIPTS="$HERE/mod/modNativeQuestHook/content/scripts"
OUTW="$(cygpath -w "$OUT")"
# REDkit's r4data may hold its own copy of this mod's script, for compiling mods that use it (Progress of the Path, say).
# Compiling the patch on top of that copy defines everything twice, so compile against a copy of r4data's scripts without it.
BASE="$(mktemp -d)"; trap 'rm -rf "$BASE"' EXIT
cp -r "$REDKIT/r4data/scripts/." "$BASE"
grep -rlis --include='*.ws' "^class CNQH_Registry" "$BASE" | while read -r f; do echo "leaving out r4data's copy: ${f#$BASE/}"; rm -f "$f"; done
cd "$REDKIT/bin/x64_RedKit" && MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' ./wcc_lite.exe compilescripts "$(cygpath -w "$BASE")" "-patch=$(cygpath -w "$SCRIPTS")" "-out=$OUTW" -agreetoterms > "$OUTW/out.txt" 2>&1
grep -E "Success! Patch|\[Script\]: Error" "$OUTW/out.txt" | cut -c26-220
if [ -f "$OUT/blob.rsblob" ] && ! grep -q "\[Script\]: Error" "$OUTW/out.txt"; then
	cp "$OUT/blob.rsblob" "$HERE/mod/modNativeQuestHook/content/precompiled.rsblob" && echo "copied to the mod's precompiled.rsblob"
fi
