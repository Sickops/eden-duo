# Castlevania: Belmont's Curse companion

A map and equipment panel for the second screen while Castlevania: Belmont's Curse runs on the
main one. The bottom screen opens on the game's own Map tab layout: the current area with
explored-zone fog, discovered landmark engravings and name ribbons, the active main objective,
and an Area/Sub-area zoom rail with Reset for the local pan/zoom view. A touch tab switches to
Equipment: the equipped Arcana card, weapon illustration, relic icons and names, and final
ATK/DEF/HP/MP, read the same way the game's own menu shows them.

Tap Pin to choose one of the game's nine native marker symbols, then tap the map to place it;
tap an existing pin to change or remove it. Equipment categories open a scrolling grid of your
owned items. Both pin edits and equipment changes go directly through the game's own marker and
inventory functions — a small helper calls `MapMarkersComponent::Add/ChangeMarkerType/RemoveMarker`
and the matching equip calls on the game's own thread, the same way the game itself would, so the
change is saved exactly as if made from the native menu. If the helper is ever unavailable the
companion falls back to driving the native menu with ordinary button presses instead of skipping
the action. Page switches, the pin chooser and map state changes use short fades, and three
haptic pulses — a light tap for navigation, a firmer one for a gameplay change that just took
effect, and a short double for a refused action — give the second screen the same feel as a
native menu.

## Supported input

- Title `0100BA4026E94000`, supported game version 1.1.0. The companion checks the running build
  before it loads; on any other version it does not load.
- Eden Duo 1.3.2 or newer (`min_runtime` 22).
- Tested on an AYN Thor (Android arm64, NCE), English.

## What the module does

`native/modules/0100BA4026E94000.cpp` and the rest of `native/modules/belmont_*`:

- Reads the game's own map geometry, explored-zone fog, area/biome metadata, discovered
  landmarks, the active objective, equipped Arcana/weapon/relics and final stats from the
  player's running game and installed RomFS, at runtime.
- Decodes the game's own menu art, the discovered-landmark engravings and name ribbons, and the
  Latin small-caps font the game's own menus use, from the player's own installation; nothing is
  bundled.
- Places, changes and removes map pins, and equips Arcana/weapon/relics, through a format-2
  guest helper (`dualscreen/guest/<build>/load-plan.json`) patched into the game at load time: the
  helper calls the game's own marker and inventory functions on the game's own thread, so pin and
  equipment data stays exactly as the game's own menus would leave it. No module ever writes
  managed game memory directly.

## Game assets

Nothing from the game ships. The package holds only the manifest, an authored font request
descriptor with no glyphs, the guest helper's load plan, and the compiled native modules.

## Building

```sh
native/modules/belmont_build.sh <eden-duo source root> <output dir>
python3 tools/build_dualscreen_package.py --package packages/BelmontsCurse \
  --output <output dir> --version 1.0.0 \
  --module linux-x86_64=<built .so> --module android-arm64-v8a=<built .so> \
  --build-id <build id from the manifest>
python3 tools/compact_zip.py <output dir>/0100BA4026E94000.dsmod.zip
```

`native/modules/belmont_build.sh` also builds and runs the module's own test suite
(`belmont_*_test`) against the player's recognized RomFS layout.

## Credits

- Castlevania: Belmont's Curse by KONAMI. A fan project, not affiliated with or endorsed by
  KONAMI or the Eden project.
- Eden Duo by igawa6.

Companion code: GPL-3.0-or-later.
