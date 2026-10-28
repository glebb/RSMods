# RSMods for macOS (RS Re-ARMed)

The Mac mods library: RSMods' features for RS Re-ARMed, the native Apple Silicon port of Rocksmith 2014. It is one
library, `rearmed-mods.dylib`, that RS Re-ARMed 1.2.0 and later load from
`~/Library/Application Support/RS Re-ARMed/mods/`. How it works, what each mod does on macOS and how it differs
from Windows: [docs/macos.md](../docs/macos.md).

## Building

Needs the Xcode command line tools (`xcode-select --install`). The game isn't needed to build.

```bash
./build.sh              # build/rearmed-mods.dylib
./build.sh --install    # also copy it into RS Re-ARMed's mods folder (quit the game first)
```

Building the configurator on macOS builds the library too, and its Home tab installs, updates and removes it (see
[docs/macos.md](../docs/macos.md)).

`RS_NATIVE_HOME` overrides the RS Re-ARMed folder. On its first start with the library, the game creates
`RSMods.ini` in its game folder (`~/Library/Application Support/RS Re-ARMed/game/`) with every mod off.

## Layout

```
build.sh                 builds build/rearmed-mods.dylib (--install: into RS Re-ARMed's mods folder)
VERSION                  the library's version
src/rsmods.c, .h         the core: hooks, game state, Wwise calls, the mod list
src/rsmods_ini.c         RSMods.ini (defaults, key names)
src/rsmods_overlay.c     text over the game
src/rsmods_scene.c       the scene graph and render steps: finding and hiding by name, the dump
src/mod_*.c              one file per mod
include/rs-rearmed/      RS Re-ARMed's runtime headers (MIT)
```

A mod is an `rsm_mod` (`rsmods.h`): a tick each frame, key events, song changes, Wwise event filtering. Add it to
`MODS` in `rsmods.c` and its defaults to `DEFAULT_INI` and `DEFAULTS` in `rsmods_ini.c`, using the same section and
key names as `mods.manifest.json`. A mod that needs a new hook point needs it in RS Re-ARMed's `hooks.skip` and a
new RS Re-ARMed release.

## Diagnostics

For finding what a mod should hook or hide. None of these are in the configurator; all are off unless set by hand,
in `RSMods.ini` or, for the log, the environment.

| Setting | What it does |
|---|---|
| `RS_MODS_LOG=1` (environment) | Logs what the mods do to stderr, which RS Re-ARMed writes to `logs/last-run.log`. With it, turning the loft off also logs every camera in memory. |
| `[Keybinds] SceneDumpKey` | Writes every scene object and render step, with its class and name, to `scene-dump.txt` in the game folder. |
| `[Toggle Switches] HideNodes` | Scene objects to hide, comma separated (`name*` matches a prefix). |
| `[Toggle Switches] HideSteps` | Render steps to switch off, comma separated. |
| `[Keybinds] HideNodesKey` | Turns `HideNodes` and `HideSteps` off and on. |
| `[Mod Settings] LoftExtraFar` | Camera far planes the loft cuts besides 10000, comma separated. |
