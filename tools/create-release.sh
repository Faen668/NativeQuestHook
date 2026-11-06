#!/bin/sh
# Makes the installable zip: unzipped into the game folder, it puts
#   bin/x64_dx12/NativeQuestHook.asi
#   mods/modNativeQuestHook/    (with its README.txt and the LICENSE)
#
#   sh tools/create-release.sh [version]
#
# The version defaults to the tag on the current commit (v1.0.0 and 1.0.0 both work).
# It builds from the last commit, not the working folder, so uncommitted changes are never shipped,
# and it never installs into the game. The zip goes in build/release.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"

VERSION="${1:-$(git tag --points-at HEAD | head -n 1)}"
if [ -z "$VERSION" ]; then
	echo "No version given, and the current commit has no tag. Run: sh tools/create-release.sh 1.0.1"
	exit 1
fi
VERSION="v${VERSION#v}"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "Note: there are uncommitted changes. They are left out: the release is built from $(git rev-parse --short HEAD)."
fi

WORK="$HERE/build/release/work"
rm -rf "${WORK:?}"
mkdir -p "$WORK/src" "$WORK/stage/bin/x64_dx12" "$WORK/stage/mods"
git archive HEAD | tar -x -C "$WORK/src"

echo "Building NativeQuestHook.asi..."
# The build's own install step is pointed at a folder in the work area, so the game is left alone
cmake -S "$WORK/src" -B "$WORK/cmake" -G "Visual Studio 17 2022" -A x64 -DW3_GAME_DIR="$WORK/nogame" > "$WORK/configure.log" 2>&1 \
	|| { echo "CMake failed to configure: see $WORK/configure.log"; exit 1; }
cmake --build "$WORK/cmake" --config Release > "$WORK/build.log" 2>&1 \
	|| { echo "The build failed: see $WORK/build.log"; exit 1; }

echo "Compiling the scripts with wcc_lite..."
COMMITTED="$WORK/committed.rsblob"
cp "$WORK/src/mod/modNativeQuestHook/content/precompiled.rsblob" "$COMMITTED"
# The archive's own compile.sh writes into the archive's copy of the mod
sh "$WORK/src/tools/compile.sh" "$WORK/scripts"
if grep -q "\[Script\]: Error" "$WORK/scripts/out.txt" || [ ! -f "$WORK/scripts/blob.rsblob" ]; then
	echo "The scripts didn't compile: see $WORK/scripts/out.txt"
	exit 1
fi
# The blob's first 24 bytes hold a build timestamp, so only what follows is compared
if ! cmp -s -i 24 "$COMMITTED" "$WORK/src/mod/modNativeQuestHook/content/precompiled.rsblob"; then
	echo "Note: the committed precompiled.rsblob was out of date. The release has a fresh one; run sh tools/compile.sh and commit it."
fi

cp "$WORK/cmake/Release/NativeQuestHook.asi" "$WORK/stage/bin/x64_dx12/"
cp -r "$WORK/src/mod/modNativeQuestHook" "$WORK/stage/mods/"
cp "$WORK/src/LICENSE" "$WORK/stage/mods/modNativeQuestHook/"

# Windows' own tar writes a standard zip, with forward slashes that every mod manager reads
ZIP="$HERE/build/release/NativeQuestHook-$VERSION.zip"
rm -f "$ZIP"
/c/Windows/System32/tar.exe -a -c -f "$(cygpath -w "$ZIP")" -C "$(cygpath -w "$WORK/stage")" bin mods

echo
/c/Windows/System32/tar.exe -t -f "$(cygpath -w "$ZIP")" | grep -v '/$'
echo
echo "Made $ZIP"
