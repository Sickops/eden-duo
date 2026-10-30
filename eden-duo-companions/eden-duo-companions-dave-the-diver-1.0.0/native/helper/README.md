# Belmont GUEST HELPER (format-2 load plan)

In-game guest helper for the Castlevania (Belmont's Curse) companion, mirroring
the BOTW companion's proven format-2 mechanism (the BOTW companion's format-2
hook builder + `packaging/duo-110-docs-r18/native/modules/botw_bridge.h`). It
lets the companion add/change/remove native map pins and equip Arcanas,
weapons and relics by calling the game's own functions, with no native menu
driving. Integrated and desktop-verified 2026-10-23 (section 8).

Title `0100BA4026E94000`, build
`0F1169AA7D0FA4529C861F96ADEC306E00000000000000000000000000000000`.

## 1. Binary layout

The dumped `main` image (passed via `--main-bin` / `BELMONT_MAIN_BIN`) is a **flat memory
image**: file offset == VA offset from `main`'s load base. Verified directly,
not assumed:

- `game-cache/0100BA4026E94000-v65536/exefs/main`'s real NSO0 header gives
  build id `0F1169AA7D0FA4529C861F96ADEC306E...` -- an exact match for the
  RE-pinned build -- and segments text@0/0x5133368, rodata@0x5134000/
  0x176B000, data@0x689F000/0x6D1A80, bss 0x39B580.
  `data.location + data.size + bss_size = 0x730C000` (120,635,392 bytes),
  exactly `main.bin`'s file size.
- `main.bin[0x40D4F00:0x40D4F04] == fd7bbda9` (`stp x29,x30,[sp,#-0x30]!`),
  matching `ApplicationFlowManager.Update()`'s entry per
  `RE-GUEST-HELPER-TARGETS-20261023.md` H1, byte-for-byte.
- `main.bin[0x1DF0320:0x1DF0324]` decodes to `BL 0x5BE780` and
  `main.bin[0x1DF033C:0x1DF0340]` to `BL 0x1C311A0` -- independently
  re-disassembled (not just taken from the RE doc's prose) to confirm the
  IL2CPP object-new call `TryPlaceMarker` makes before `MapMarkerData..ctor`.

`tools/belmont/build_helper_plan.py` parses the real NSO header for `layout`
(it does not decompress the NSO -- `main.bin` already is the decompressed
image, which is what `expected` bytes and `main`-targeted relocation addends
are checked against) and asserts the hook bytes still match `main.bin` before
emitting a plan.

## 2. Hook

Patch: `main`, offset `0x40D4F00`, `expected = fd7bbda9`,
`replacement = b <code+0>` (`aarch64_branch26`, target `code`). Only `x0`
(`this`) is live-in at function entry per the RE notes; every other register
Update() uses (`x19`-`x21`, `x29`, `x30`, `sp`) is either saved by Update()'s
*own* prologue immediately after we resume, or is the stack/frame pointer
itself, which the hook must leave exactly as a plain
`stp x29,x30,[sp,#-0x30]!` would.

**Frame strategy -- "reproduce and resume" (chosen over "own frame + emulate
original").** The helper's first instruction re-executes the identical
`stp x29,x30,[sp,#-0x30]!` byte-for-byte, which opens Update()'s frame exactly
as the unpatched game would; the next three Update() instructions
(`str x21,[sp,#0x10]`; `stp x20,x19,[sp,#0x20]`; `mov x29,sp`) read/write that
same stack region and must see it set up identically. The alternative --
giving the helper its own frame and manually replaying what `stp` does with
different bookkeeping -- would have to reconstruct the exact same bytes
anyway, with more room for a subtle mismatch, for no benefit. Having
reproduced that one instruction, the helper opens a **second, disposable**
scratch frame directly below it (`sub sp,sp,#0xA0` ... `add sp,sp,#0xA0`) to
save/restore every register it touches (`x0`, `x19`-`x28`, `x30`, `d8`-`d15`)
before branching to `0x40D4F04`. Net stack effect when execution resumes:
identical to having executed only the original `stp`. Only `x0` is proven
load-bearing by the RE notes, but the rest are saved anyway, defensively, per
the task's own "preserve all callee-saved and argument registers" directive --
cheap (10 extra instruction pairs) and removes an entire class of risk if
Update()'s un-disassembled tail turns out to use more registers than the
visible prologue suggests.

Verified via `llvm-objdump -d` on the compiled object (not just assembled by
eye): every internal branch target lands on the intended label, the resume
branch is the last instruction in the blob, and `belmont_scan_owned`'s
`bl`/`ret` pair is a local call within the same translation unit (safe: it
clobbers `x30` only inside the helper's own already-saved call chain, never
the value Update() needs back).

## 3. Mailbox layout (4096 bytes, epoch at 4092)

Byte offsets (also `static_assert`-checked against a `#pragma pack(1)`
struct in `native/modules/belmont_helper_mailbox.h`, so drift between this
table, the header and the `.S` file's literal immediates is a compile error
for the header/struct pair; the `.S` side is the one place a change has to be
made by hand in both files):

| Offset | Size | Field | Writer | Notes |
|---|---|---|---|---|
| 0x000 | u32 | `magic` | module | `0x32504C42`; unset -> helper touches nothing else that tick |
| 0x004 | u32 | `heartbeat` | guest | incremented every tick the helper runs, even idle |
| 0x008 | u32 | `req_seq` | module (release) | new request iff `req_seq != result_seq`; guest acquire-reads it |
| 0x00C | u32 | `req_op` | module | 1=ADD_PIN 2=REMOVE_PIN 3=CHANGE_PIN 4=EQUIP_SPELL 5=EQUIP_WEAPON 6=EQUIP_CHARM |
| 0x010 | u32 | `req_epoch` | module | mirrors the epoch word at 0xFFC as of staging |
| 0x014 | u32 | _reserved_ | -- | |
| 0x018 | u64 | `req_primary_ptr` | module | op-dependent, see table below |
| 0x020 | u64 | `req_secondary_ptr` | module | op-dependent |
| 0x028 | u64 | `req_tertiary_ptr` | module | CHANGE_PIN only |
| 0x030 | u32 | `req_arg_a` | module | op-dependent |
| 0x034 | u32 | `req_arg_b` | module | op-dependent |
| 0x038 | u32 | `req_arg_c` | module | op-dependent |
| 0x03C | u32 | _reserved_ | -- | |
| 0x040 | u64 | `ctx_markers_component` | module, every tick | current `MapMarkersComponent*` |
| 0x048 | u64 | `ctx_inventory` | module, every tick | current `Inventory*` |
| 0x050,0x058 | u64x2 | _reserved_ | -- | |
| 0x060 | u32 | `result_seq` | guest (release) | `= req_seq` once answered |
| 0x064 | u32 | `result_code` | guest | see refusal-code table |
| 0x068 | u64 | `result_value` | guest | op-dependent (e.g. new marker id) |
| 0x070 | u64x16 | `class_table[16]` | module, once per klass | see slot table below; 8-15 reserved |
| 0x0F0..0xFFB | -- | _reserved_ | -- | 3852 bytes spare for future ops without a mailbox resize |
| 0xFFC | u32 | epoch | host (pause/invalidate) + module (initial seed) | the load plan's `mailbox_epoch_offset` word |

Per-op field usage:

| Op | primary_ptr | secondary_ptr | tertiary_ptr | arg_a | arg_b | arg_c | result_value |
|---|---|---|---|---|---|---|---|
| ADD_PIN | `LevelID*` | `MapMarkerTypeID*` (klass-checked, then dereferenced to its `+0x18` guid `String*`) | -- | x (f32 bits) | y (f32 bits) | `navigationZoneIndex` | new `MapMarkerData.id` |
| REMOVE_PIN | `LevelID*` | -- | -- | `marker_id` | -- | -- | 0 |
| CHANGE_PIN | `LevelID*` | `MapMarkerData*` | `MapMarkerTypeID*` | -- | -- | -- | 0 |
| EQUIP_SPELL | `SpellItemData*` | -- | -- | -- | -- | -- | 0 |
| EQUIP_WEAPON | `WeaponItemData*` | -- | -- | -- | -- | -- | 0 |
| EQUIP_CHARM | `CharmItemData*` | -- | -- | slot (s32, -1=auto) | -- | -- | slot actually used |

`class_table[]` slots (module fills each once; a klass pointer is a
process-lifetime constant). The opcode fixes which slot(s) the guest checks
-- there is no per-request class-tag field, since the mapping never varies:

| Slot | Expected klass | Checked for |
|---|---|---|
| 0 | `MapMarkerData` | CHANGE_PIN secondary; ADD_PIN's allocation |
| 1 | `LevelID` (concrete) | ADD_PIN/REMOVE_PIN/CHANGE_PIN primary |
| 2 | `MapMarkerTypeID` | ADD_PIN secondary; CHANGE_PIN tertiary (same slot, same object shape) |
| 3 | `SpellItemData` | EQUIP_SPELL primary |
| 4 | `WeaponItemData` | EQUIP_WEAPON primary |
| 5 | `CharmItemData` | EQUIP_CHARM primary |
| 6 | `MapMarkersComponent` | `ctx_markers_component` |
| 7 | `Inventory` | `ctx_inventory` |

Refusal codes (`belmont_helper_mailbox::Result`, `result_code`):

| Code | Meaning |
|---|---|
| 0 | OK |
| 1 | NOT_CONFIGURED -- magic or the relevant `class_table` slot not seeded yet |
| 2 | BAD_OP -- `req_op` outside `[1,6]` |
| 3 | STALE_EPOCH -- `req_epoch` != the 0xFFC word, or that word is 0 |
| 4 | LOADING -- `ApplicationFlowManager._isLoading != 0`, or its static chain unresolved |
| 5 | NULL_CONTEXT -- `ctx_markers_component`/`ctx_inventory` null, or its klass doesn't match |
| 6 | NULL_PRIMARY |
| 7 | NULL_SECONDARY |
| 8 | NULL_TERTIARY |
| 9 | CLASS_MISMATCH_PRIMARY |
| 10 | CLASS_MISMATCH_SECONDARY |
| 11 | CLASS_MISMATCH_TERTIARY |
| 12 | NOT_OWNED -- equip item not found in `Inventory._itemsInInventory` |
| 13 | ALLOC_FAILED -- ADD_PIN's `il2cpp` object-new returned null |

The mailbox protocol is a classic release/acquire sequence-number handshake
(same shape as BOTW's `botw_bridge.h`/`native-ui-hook.S`): the module writes
every other request field with a plain store, then `req_seq` last via
`store_u32` (release, per `dsmod_module_extensions.h`'s documented
contract); the guest acquire-loads `req_seq` (`ldar`) before reading
anything else, and symmetrically publishes `result_code`/`result_value`
first, `result_seq` last via `stlr`.

## 4. Why the guest only does *cheap* re-validation, and where its pointers actually come from

(Updated 2026-10-23 with a live residency probe.)

The module (host-side C++, already has full process-memory read access, the
same way `belmont_reader.h`/`belmont_map_control.h`/`belmont_inventory.h`
resolve pointers today) does the *expensive* work every tick: walking
`GameStateManager`'s static singleton to the player `Entity`, then its
component dictionary, to get fresh `MapMarkersComponent*`/`Inventory*`
pointers, and writing them into `ctx_markers_component`/`ctx_inventory`. The
guest helper only does the cheap, TOCTOU-closing re-check immediately before
it calls into game code: non-null, and the object's own klass pointer
(`*(object+0)`) matches the module-filled `class_table[]` entry. This is the
same split the task description asks for ("the module resolves and validates
pointers... and the helper re-validates cheaply... before calling").

An integrator's live, read-only probe
(the private live residency probe of 2026-10-23,
desktop CLI, GDB RSP reads only) resolved the one piece this design left
open: **which `LevelID*`/`MapMarkerTypeID*` the module should pass.** The
`LevelMapMarkersController` chain this design's first draft cited (and
flagged as menu-dependent, see the superseded notes kept in §7) is
**confirmed NOT resident with the native Map menu closed**
(`UIMapLevelManager._currentMapObject`, `+0x130`, is null). Two different,
always-resident registries replace it, and both were live-read, not just
disassembled:

- **Marker types:** `UIManager_TypeInfo` (`main+0x6D47F10`) -> klass ->
  static fields (`+0xB8`) -> `_instance` -> `mapMarkerTypeDisplayDataDictionary`
  (`+0x58`) -> `elements` List (`+0x18`) -> items (`+0x10`)/size (`+0x18`) ->
  each element's `+0x18` = a `MapMarkerTypeID*`. Live: exactly 9 elements,
  pointer-identical to the 9 placeable types.
- **Levels:** `LevelManager.mainFlowLevelIDs` (`+0x70`, a `LevelID[]`),
  selected by `LevelID.guid` (`+0x18`) -- the module already derives the
  current level's GUID for pin display, so it just matches that GUID against
  this array's elements. `ExplorerComponent.justEnteredLevelID` is confirmed
  **null in live play** (the fallback §7 of the first draft flagged as
  unproven is simply unusable, not merely unverified).

Both registries resolve with the menu open *or* closed, so **ADD_PIN/
REMOVE_PIN/CHANGE_PIN no longer need to be gated on the native Map menu being
open** (the first draft of this README recommended that gate out of an
abundance of caution; it is superseded by this probe). `CHANGE_PIN`'s
tertiary and `ADD_PIN`'s secondary are literally the same kind of pointer
(a `MapMarkerTypeID*` from this same 9-element registry), which is why they
share `class_table[2]`.

**AddMarker creates a level's first `LevelMarkersData` entry itself.** The
probe's Q3 found the current level (GUID `794afe09...`) is *not yet a key* in
`MapMarkersComponent._levelsMarkersData` (it has never had a pin), which
raises the obvious question: does `AddMarker` require a pre-existing key?
Direct disassembly of `main.bin` at `0x1C2E7A0`..`0x1C2E874` answers this:
`AddMarker` calls the dictionary's generic `TryGetValue` (`bl 0x392B860`),
and on a miss, allocates a **new** object via the exact same
`bl 0x5BE780` IL2CPP object-new allocator the helper itself uses for
`MapMarkerData` (see §1), constructs it (`bl 0x1C2E990`, a function with its
own full `stp x29,x30,...` prologue) and inserts it into the dictionary
(`bl 0x3929CE0`, a generic `set_Item`) before both paths converge and call
`LevelMarkersData.AddMarkerData` (`0x1C2E8B0`, already in the RE doc).
**ADD_PIN therefore needs no "ensure the level has an entry first" step**: a
level that has never had a pin works exactly like one that has.

The one piece the guest *does* resolve directly, itself, every tick, is
`ApplicationFlowManager._isLoading`: it is a single two-pointer-dereference
read from a fixed address (`base+0x6D3F780` -> klass-ptr cache ->
`klass+0xB8` static-fields blob -> byte 0), exactly how
`belmont_reader.h::Flag(base, 0x6d3f780, "ApplicationFlowManager", ...)`
already reads it, and reading it directly removes one more field the module
would otherwise have to keep mirrored every tick, shrinking the staleness
window for the single most safety-critical check (RE H6: calling into
`AddMarker`/`EquipXFromInventory` during loading/title null-derefs on the
game's own `cbz` guards, a native crash, not a safe refusal).

## 5. Known gaps (by design, for this offline skeleton)

- **No existence pre-scan for REMOVE_PIN/CHANGE_PIN's marker pointers beyond
  the klass check.** RE H3 shows both natively no-op safely on a stale id/
  pointer (`RemoveMarker` on an absent id; `ChangeMarkerType`'s null guards),
  so this is a correctness nicety, not a safety requirement, and was cut to
  keep the scope of a from-scratch, offline, unverified assembly skeleton
  sane. Re-add if live testing shows a real desync.
- **ADD_PIN is the one op that allocates** (`bl belmont_il2cpp_object_new`
  then the ctor) -- and, per §4's disassembly of `AddMarker` itself, can
  allocate a *second* time (a new `LevelMarkersData`) when the target level
  has never had a pin before. RE H6 flags allocation as the highest-risk
  behaviour and offers "restrict to change/remove/equip only" as a
  mitigation; this skeleton implements ADD_PIN anyway (the task asks for it)
  but it is the one op most worth extra scrutiny in live testing before
  trusting it, and doubly so for a brand-new level.
- **ADD_PIN's secondary pointer is klass-checked as a `MapMarkerTypeID*`
  (`class_table[2]`, shared with CHANGE_PIN's tertiary), not as the
  `System.String` it is ultimately dereferenced into** -- the guest trusts
  the one dereference (`ptr+0x18`) into that already-klass-validated object,
  the same way `TryPlaceMarker` itself does, rather than also klass-checking
  the resulting string. The module should source the `MapMarkerTypeID*` from
  the live-confirmed `UIManager_TypeInfo` registry (§4), not from
  `LevelMapMarkersController.placeableMarkers` (confirmed not resident with
  the menu closed).
- **`req_class_tag` was deliberately removed** from an earlier draft of the
  layout: every op fixes its own `class_table` slot(s), so a per-request tag
  field would have been redundant. If a future op needs a *variable* class
  per request (not fixed by opcode), that field will need to come back.

## 6. Build and test

```sh
# Builds .scratch/helper-out/plan.json (+ .o/.bin/relocations.txt), verifying
# main.bin's hook bytes and the NSO build id/layout along the way.
python3 tools/belmont/build_helper_plan.py

# Module-side client, fake-mailbox round trip.
g++ -std=c++20 -Wall -Wextra -Werror \
    native/modules/belmont_helper_mailbox_test.cpp \
    -o /tmp/belmont_helper_mailbox_test
/tmp/belmont_helper_mailbox_test
```

Both were run while writing this skeleton:

- `build_helper_plan.py`: compiles cleanly, 1160 bytes of guest code (290
  instructions, well under the 64 KiB cap), 13 relocations (2 mailbox + 2
  loading-flag + 9 call/branch), every `main`-targeted addend matches the
  RE-pinned VA it names, hook `expected` bytes confirmed against `main.bin`.
- `belmont_helper_mailbox_test`: layout offsets, `Initialize`/`SetClass`/
  `SetContext`/`SyncEpoch`/`IsAlive`, a full ADD_PIN-shaped stage/poll round
  trip (including the release/acquire write order), busy-gating (a second
  `TryStage` while one is in flight is refused and does not corrupt the
  in-flight request), and refusal reporting via `Outcome::code` all pass.

Neither step launches the emulator.

## 7. Open questions for live verification (see section 8 for what the 2026-10-23 live run settled)

- ~~Which `LevelID*`/marker-type pointers the module should actually pass~~
  **RESOLVED** by `HELPER-RESIDENCY-20261023.md` (live probe): the module
  resolves both from the always-resident `UIManager_TypeInfo` and
  `LevelManager.mainFlowLevelIDs` registries (§4), not from
  `LevelMapMarkersController` (confirmed not resident with the menu closed).
  `ADD_PIN`/`REMOVE_PIN`/`CHANGE_PIN` no longer need a menu-open gate.
- ~~Whether `AddMarker` needs a pre-existing dictionary entry for the
  level~~ **RESOLVED** by direct disassembly (§4): it creates one lazily.
- **The allocator address (`0x5BE780`).** Confirmed independently, twice now
  (this pass's §1, and again while tracing `AddMarker`'s own lazy-creation
  path in §4, which calls the *same* address) by decoding raw AArch64 `BL`
  instructions in `main.bin`, not by trusting any RE doc's prose alone --
  but it has still never been *called* by this helper, only disassembled.
  Live-test ADD_PIN before trusting it in a release; it is the op that
  exercises this address, and the only op that can allocate twice in one
  call (a fresh `LevelMarkersData` and a fresh `MapMarkerData`).
- **Whether a map-UI-driven `TryPlaceMarker`/`TryChangeMarker` transaction
  racing the guest helper on the same tick is safe.** RE H6 flags this as
  untested by any static analysis, and the residency probe did not exercise
  it either (it was a read-only probe, no writes). Still worth a dedicated
  live check, now that the ops are no longer gated on the menu being closed
  -- the race is if anything *more* reachable than before, since the helper
  can now also run while the menu is open and the native palette is active.
- **Exact count/order of the marker-type registry.** The residency probe
  confirms 9 live elements in `UIManager_TypeInfo`'s dictionary, matching the
  expected 9 placeable types and resolving the earlier doubt about
  `LevelMapMarkersController.placeableMarkers`'s count -- but their mapping
  to in-game marker *names* (which index is "camp", "shop", etc.) was not
  part of this probe and still needs a live cross-check before the module
  hardcodes any index-to-name assumption.

## 8. Integration (done 2026-10-23) and live verification

- **Package:** `packages/BelmontsCurse/dualscreen/guest/<BUILD>/load-plan.json`
  (copied from `build_helper_plan.py` output), manifest
  `"load_plan": "guest/<build>/load-plan.json"`, provenance entry.
- **Module:** `0100BA4026E94000.cpp` binds `belmont_helper_mailbox::Client` in
  `Configure()` when the host supplies a mailbox, seeds magic/epoch, and every
  sample: polls the in-flight request, checks the heartbeat (helper "usable"
  for 500 ms after the last change), refreshes `class_table[]` and the
  registries (`belmont_helper_refs.h`) once a second, and writes the context
  pointers. Requests are staged only in field gameplay (`initial_identity==1`,
  not paused) so they never race a native menu transaction.
- **Pins:** each companion pin intent is staged as ADD/CHANGE/REMOVE when the
  helper is usable; a refusal (always before any game call) falls back to the
  existing menu steering; a 2 s timeout never falls back (the call may still
  land) and retires the helper for the session.
- **Equipment:** with the helper usable and the native menu closed, the
  Armoury/Arcanas/Relics buttons open a companion picker built from
  `Inventory._itemsInInventory`; a pick stages EQUIP_*. Relics use slot -1
  (first free) or, with every slot worn, a "Choose relic slot" step that
  passes the explicit slot. Otherwise the native-menu picker is unchanged.
- **Fixes found while integrating:** weapons are concrete subclasses
  (`WeaponFistItemData`, ...), so the equip ops check class with a bounded
  parent walk (`belmont_is_a`, klass+0x58), not an exact compare. Pins loaded
  from a save leave the lazy `MapMarkerData` TypeInfo slot as the metadata
  token, so its klass comes from a live `MapMarkerData` object.

Live desktop result (runtime 22 eden-cli, late save, Master's Quarters):
ADD (allocator `0x5BE780` exercised), CHANGE and REMOVE each answered OK with
no native button pulses; the native Map then showed the helper-added pin and
an updated remaining-marker count. EQUIP_SPELL (LifeDrain -> CursedFireBall),
EQUIP_WEAPON (whip -> Cestus, a subclass) and EQUIP_CHARM with an explicit
slot all answered OK and the native Equipment tab matched. Not yet verified:
save persistence across a save/reload, Android/NCE on the Thor.
