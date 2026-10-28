#!/bin/sh
# Build rearmed-mods.dylib, RSMods' features for RS Re-ARMed.
#   ./build.sh              -> build/rearmed-mods.dylib
#   ./build.sh --install    also copy it into RS Re-ARMed's mods folder
#                           ($RS_NATIVE_HOME/mods, default ~/Library/Application Support/RS Re-ARMed/mods),
#                           which RS Re-ARMed 1.2.0 and later load at startup. Quit the game first.
# The library uses RS Re-ARMed's runtime (rc_call_guest, rc_slide, rc_hook_add), resolved when the game loads it;
# the declarations are in include/rs-rearmed (from RS Re-ARMed, MIT). It builds without the game.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
install=0
[ "${1:-}" = --install ] && install=1
out=$here/build
mkdir -p "$out/obj"
version=$(cat "$here/VERSION")

CC="clang -arch arm64 -O2 -g -mmacosx-version-min=11.0 -Wall -Wno-unused-function -ffp-contract=off -fno-strict-aliasing -I$here/src -I$here/include/rs-rearmed -DREARMED_MODS_VERSION=\"$version\""
objs=""
for c in "$here"/src/*.c; do
  o="$out/obj/$(basename "$c" .c).o"
  $CC -c "$c" -o "$o"
  objs="$objs $o"
done
clang -arch arm64 -dynamiclib -mmacosx-version-min=11.0 -install_name @rpath/rearmed-mods.dylib \
  -Wl,-undefined,dynamic_lookup $objs -lobjc -framework OpenGL -framework CoreText -framework CoreGraphics \
  -framework CoreFoundation -framework CoreAudio -framework ImageIO -o "$out/rearmed-mods.dylib"
codesign --force -s - "$out/rearmed-mods.dylib" 2>/dev/null || true
echo "rearmed-mods $version: $out/rearmed-mods.dylib"

if [ $install = 1 ]; then
  home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
  [ -d "$home/game" ] || { echo "no RS Re-ARMed install at $home" >&2; exit 1; }
  if pgrep -xq Rocksmith2014; then echo "quit the game first" >&2; exit 1; fi
  mkdir -p "$home/mods"
  # a new file renamed over the old one: overwriting a loaded library in place breaks the running game and
  # leaves macOS with the old code signature cached for the file
  cp "$out/rearmed-mods.dylib" "$home/mods/rearmed-mods.dylib.new"
  mv -f "$home/mods/rearmed-mods.dylib.new" "$home/mods/rearmed-mods.dylib"
  echo "installed: $home/mods/rearmed-mods.dylib"
fi
