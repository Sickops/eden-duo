// SPDX-License-Identifier: GPL-3.0-or-later
//
// Castlevania (Belmont's Curse) guest helper -- format-2 load-plan code.
// Title 0100BA4026E94000, build 0F1169AA7D0FA4529C861F96ADEC306E.
//
// Hooked site: ApplicationFlowManager.Update(), VA/file-offset 0x40D4F00, entry
// instruction `stp x29, x30, [sp, #-0x30]!` (bytes fd7bbda9). The load plan replaces
// that one instruction with `b belmont_guest_helper_entry` (aarch64_branch26, target
// "code"). See native/helper/README.md for the full design writeup, the mailbox
// layout table and the exact integration edits an integrator still needs to make.
//
// Frame strategy ("Variant A: reproduce and resume", see README "Hook frame
// strategy"): the helper's FIRST instruction re-executes the exact overwritten
// `stp x29, x30, [sp, #-0x30]!`, which opens Update()'s own frame bit-for-bit
// identically to the unpatched game -- Update()'s own next instructions
// (str x21,[sp,#0x10]; stp x20,x19,[sp,#0x20]; mov x29,sp; ...) depend on that exact
// layout and must see it. The helper then opens its OWN, separate, disposable
// scratch frame below that (sub sp,sp,#0xA0 ... add sp,sp,#0xA0), so by the time it
// branches back to 0x40D4F04 the stack pointer is back to exactly where the single
// reproduced `stp` left it -- indistinguishable from the unpatched function at that
// point. Only x0 (`this`) is live-in per the RE notes; everything else the helper
// might touch (x19-x28, x30, d8-d15) is saved and restored anyway, defensively,
// exactly as asked ("preserves all callee-saved and argument registers").
//
// External symbols below are deliberately left undefined; tools/belmont/
// build_helper_plan.py resolves each one's relocations to a (target, addend) pair
// after compiling with `clang --target=aarch64-none-elf -c` (same technique as
// the BOTW companion's format-2 hook builder).

.text
.global belmont_guest_helper_entry
belmont_guest_helper_entry:
    // --- Reproduce the overwritten entry instruction exactly. ---
    stp  x29, x30, [sp, #-0x30]!

    // --- Open a disposable scratch frame (160 B, 16-byte aligned) for the helper's
    //     own work. Everything in here is undone before we resume Update(). ---
    sub  sp, sp, #0xA0
    stp  x0,  x30, [sp, #0x00]   // x0 = `this` (must survive to 0x40D4F04); x30 = our own LR
    stp  x19, x20, [sp, #0x10]
    stp  x21, x22, [sp, #0x20]
    stp  x23, x24, [sp, #0x30]
    stp  x25, x26, [sp, #0x40]
    stp  x27, x28, [sp, #0x50]
    stp  d8,  d9,  [sp, #0x60]
    stp  d10, d11, [sp, #0x70]
    stp  d12, d13, [sp, #0x80]
    stp  d14, d15, [sp, #0x90]

    // x19 = mailbox base (target "mailbox", addend 0).
    adrp x19, belmont_mailbox_base
    add  x19, x19, :lo12:belmont_mailbox_base

    // heartbeat++ (offset 0x04). Plain load/store: only this helper ever writes it.
    ldr  w9, [x19, #0x04]
    add  w9, w9, #1
    str  w9, [x19, #0x04]

    // magic check (offset 0x00). Not yet configured -> touch nothing else this tick.
    ldr  w9, [x19, #0x00]
    movz w10, #0x4C42
    movk w10, #0x3250, lsl #16
    cmp  w9, w10
    b.ne helper_done

    // req_seq (offset 0x08), acquire -- pairs with the module's store_u32 (release).
    add  x9, x19, #0x08
    ldar w20, [x9]
    // result_seq (offset 0x60), plain -- this thread wrote it itself last tick.
    ldr  w21, [x19, #0x60]
    cmp  w20, w21
    b.eq helper_done            // no new request staged since the last ack

    // req_op (offset 0x0C) range check.
    ldr  w11, [x19, #0x0C]
    cmp  w11, #1
    b.lt refuse_bad_op
    cmp  w11, #6
    b.gt refuse_bad_op

    // epoch (offset 0x10 req_epoch vs 0xFFC host/epoch word), acquire on the latter --
    // pairs with the module's initial store_u32 seed and the host's pause/invalidate
    // bump (System::InvalidateDualScreenGuestMailbox). Zero means never seeded.
    ldr  w12, [x19, #0x10]
    add  x9, x19, #0xFFC
    ldar w13, [x9]
    cbz  w13, refuse_stale_epoch
    cmp  w12, w13
    b.ne refuse_stale_epoch

    // ApplicationFlowManager._isLoading, read the same way belmont_reader.h does:
    // base+0x6D3F780 is the (lazily-populated) klass-pointer cache slot for
    // ApplicationFlowManager; klass+0xB8 is the static-fields blob; the flag itself
    // is the first byte of that blob (field offset 0).
    adrp x9, belmont_app_flow_loading_klass_cache
    ldr  x9, [x9, :lo12:belmont_app_flow_loading_klass_cache]
    cbz  x9, refuse_loading_unknown
    ldr  x10, [x9, #0xb8]
    cbz  x10, refuse_loading_unknown
    ldrb w11, [x10, #0]
    cbnz w11, refuse_loading

    // Dispatch on req_op (reload: w11 was clobbered by the loading check above).
    ldr  w11, [x19, #0x0C]
    cmp  w11, #1
    b.eq op_add_pin
    cmp  w11, #2
    b.eq op_remove_pin
    cmp  w11, #3
    b.eq op_change_pin
    cmp  w11, #4
    b.eq op_equip_spell
    cmp  w11, #5
    b.eq op_equip_weapon
    cmp  w11, #6
    b.eq op_equip_charm
    b    refuse_bad_op          // unreachable given the range check above; defensive

// ---------------------------------------------------------------------------
// ADD_PIN: ctor(MapMarkerData) + MapMarkersComponent.AddMarker.
// req_primary_ptr=LevelID*, req_secondary_ptr=MapMarkerTypeID* (the resident
// marker-type object itself, klass-checked here then dereferenced to its
// guid String* -- same object shape as CHANGE_PIN's tertiary, same
// class_table slot),
// req_arg_a=x(float bits), req_arg_b=y(float bits), req_arg_c=nav_index(int32).
//
// AddMarker (0x1C2E7A0) itself lazily creates the LevelMarkersData dictionary
// entry (TryGetValue, and on a miss: the same belmont_il2cpp_object_new
// allocator used below, then a ctor and a dictionary insert, all before
// falling through to the common AddMarkerData tail) when `req_primary_ptr`'s
// LevelID has never had a pin before -- confirmed by direct disassembly of
// main.bin at 0x1C2E7A0..0x1C2E874, not merely asserted. The helper does not
// need to special-case a level with no existing entry.
// ---------------------------------------------------------------------------
op_add_pin:
    // MapMarkerData's TypeInfo slot is lazy: a save whose pins were loaded
    // from disk leaves it holding the metadata token until the game's own
    // TryPlaceMarker first runs. Initialize it exactly as TryPlaceMarker does
    // (0x1DF021C..0x1DF0230: x0 = &slot, bl InitializeRuntimeMetadata) before
    // any other load -- the call clobbers the caller-saved registers.
    adrp x0, belmont_map_marker_data_typeinfo
    add  x0, x0, :lo12:belmont_map_marker_data_typeinfo
    bl   belmont_il2cpp_init_runtime_metadata

    ldr  x9, [x19, #0x40]              // ctx_markers_component
    cbz  x9, refuse_null_context
    ldr  x10, [x9, #0]                   // its klass
    ldr  x11, [x19, #0xA0]                // class_table[6] = MapMarkersComponent klass
    cbz  x11, refuse_not_configured
    cmp  x10, x11
    b.ne refuse_null_context

    ldr  x12, [x19, #0x18]               // req_primary_ptr = LevelID*
    cbz  x12, refuse_null_primary
    ldr  x13, [x12, #0]
    ldr  x14, [x19, #0x78]                 // class_table[1] = LevelID klass
    cbz  x14, refuse_not_configured
    cmp  x13, x14
    b.ne refuse_class_mismatch_primary

    ldr  x15, [x19, #0x20]                // req_secondary_ptr = MapMarkerTypeID*
    cbz  x15, refuse_null_secondary
    ldr  x9, [x15, #0]
    ldr  x10, [x19, #0x80]                  // class_table[2] = MapMarkerTypeID klass
    cbz  x10, refuse_not_configured
    cmp  x9, x10
    b.ne refuse_class_mismatch_secondary
    ldr  x15, [x15, #0x18]                   // dereference to the resident guid String*

    adrp x9, belmont_map_marker_data_typeinfo
    ldr  x0, [x9, :lo12:belmont_map_marker_data_typeinfo] // initialized klass
    cbz  x0, refuse_not_configured
    tbnz x0, #0, refuse_not_configured      // still an (odd) metadata token
    ldr  x10, [x19, #0x70]                  // class_table[0], when the module knows it
    cbz  x10, add_pin_klass_ok
    cmp  x0, x10
    b.ne refuse_not_configured
add_pin_klass_ok:
    bl   belmont_il2cpp_object_new            // x0 (klass) in -> x0 (new object*) out
    cbz  x0, refuse_alloc_failed
    mov  x21, x0                               // marker_obj, kept across the next call

    mov  x0, x21
    ldr  w1, [x19, #0x38]                        // req_arg_c = navigationZoneIndex
    ldr  w9, [x19, #0x30]                         // req_arg_a = x, raw bits
    fmov s0, w9
    ldr  w9, [x19, #0x34]                          // req_arg_b = y, raw bits
    fmov s1, w9
    mov  x2, x15                                    // markerTypeGuid string*
    bl   belmont_map_marker_data_ctor

    ldr  x0, [x19, #0x40]                             // this = MapMarkersComponent*
    mov  x1, x12                                        // targetLevel
    mov  x2, x21                                          // markerData
    bl   belmont_markers_add_marker

    ldr  w9, [x21, #0x10]                                   // MapMarkerData.id, assigned by
    str  x9, [x19, #0x68]                                     // AddMarkerData; zero-extends to x9
    b    op_success

// ---------------------------------------------------------------------------
// REMOVE_PIN: MapMarkersComponent.RemoveMarker. A no-op natively if the id is
// stale, so no existence pre-scan here (see README "Known gaps").
// req_primary_ptr=LevelID*, req_arg_a=marker_id(int32).
// ---------------------------------------------------------------------------
op_remove_pin:
    ldr  x9, [x19, #0x40]
    cbz  x9, refuse_null_context
    ldr  x10, [x9, #0]
    ldr  x11, [x19, #0xA0]
    cbz  x11, refuse_not_configured
    cmp  x10, x11
    b.ne refuse_null_context

    ldr  x12, [x19, #0x18]
    cbz  x12, refuse_null_primary
    ldr  x13, [x12, #0]
    ldr  x14, [x19, #0x78]
    cbz  x14, refuse_not_configured
    cmp  x13, x14
    b.ne refuse_class_mismatch_primary

    ldr  x0, [x19, #0x40]
    mov  x1, x12
    ldr  w2, [x19, #0x30]
    bl   belmont_markers_remove_marker

    str  xzr, [x19, #0x68]
    b    op_success

// ---------------------------------------------------------------------------
// CHANGE_PIN: MapMarkersComponent.ChangeMarkerType.
// req_primary_ptr=LevelID*, req_secondary_ptr=MapMarkerData*,
// req_tertiary_ptr=MapMarkerTypeID*.
// ---------------------------------------------------------------------------
op_change_pin:
    ldr  x9, [x19, #0x40]
    cbz  x9, refuse_null_context
    ldr  x10, [x9, #0]
    ldr  x11, [x19, #0xA0]
    cbz  x11, refuse_not_configured
    cmp  x10, x11
    b.ne refuse_null_context

    ldr  x12, [x19, #0x18]
    cbz  x12, refuse_null_primary
    ldr  x13, [x12, #0]
    ldr  x14, [x19, #0x78]
    cbz  x14, refuse_not_configured
    cmp  x13, x14
    b.ne refuse_class_mismatch_primary

    ldr  x15, [x19, #0x20]              // req_secondary_ptr = MapMarkerData*
    cbz  x15, refuse_null_secondary
    ldr  x9, [x15, #0]
    ldr  x10, [x19, #0x70]                // class_table[0] = MapMarkerData klass
    cbz  x10, refuse_not_configured
    cmp  x9, x10
    b.ne refuse_class_mismatch_secondary

    ldr  x16, [x19, #0x28]               // req_tertiary_ptr = MapMarkerTypeID*
    cbz  x16, refuse_null_tertiary
    ldr  x9, [x16, #0]
    ldr  x10, [x19, #0x80]                 // class_table[2] = MapMarkerTypeID klass
    cbz  x10, refuse_not_configured
    cmp  x9, x10
    b.ne refuse_class_mismatch_tertiary

    ldr  x0, [x19, #0x40]
    mov  x1, x12
    mov  x2, x15
    mov  x3, x16
    bl   belmont_markers_change_marker_type

    str  xzr, [x19, #0x68]
    b    op_success

// ---------------------------------------------------------------------------
// EQUIP_SPELL / EQUIP_WEAPON / EQUIP_CHARM: Inventory.EquipXFromInventory.
// The native wrappers do not re-check ownership (RE H3/H6), so the helper scans
// Inventory._itemsInInventory for a pointer-identical match before calling.
// req_primary_ptr=ItemData* (concrete subtype per op); EQUIP_CHARM also uses
// req_arg_a=slot(int32, -1=auto).
// ---------------------------------------------------------------------------
op_equip_spell:
    ldr  x9, [x19, #0x48]                 // ctx_inventory
    cbz  x9, refuse_null_context
    ldr  x10, [x9, #0]
    ldr  x11, [x19, #0xA8]                  // class_table[7] = Inventory klass
    cbz  x11, refuse_not_configured
    cmp  x10, x11
    b.ne refuse_null_context

    ldr  x12, [x19, #0x18]                   // primary = SpellItemData*
    cbz  x12, refuse_null_primary
    ldr  x13, [x12, #0]
    ldr  x14, [x19, #0x88]                     // class_table[3] = SpellItemData klass
    cbz  x14, refuse_not_configured
    bl   belmont_is_a                           // in: x13=klass, x14=base; out: w0
    cbz  w0, refuse_class_mismatch_primary

    bl   belmont_scan_owned                      // in: x9=Inventory*, x12=item*; out: w0
    cbz  w0, refuse_not_owned

    ldr  x0, [x19, #0x48]
    mov  x1, x12
    bl   belmont_inventory_equip_spell
    str  xzr, [x19, #0x68]
    b    op_success

op_equip_weapon:
    ldr  x9, [x19, #0x48]
    cbz  x9, refuse_null_context
    ldr  x10, [x9, #0]
    ldr  x11, [x19, #0xA8]
    cbz  x11, refuse_not_configured
    cmp  x10, x11
    b.ne refuse_null_context

    ldr  x12, [x19, #0x18]                     // primary = WeaponItemData*
    cbz  x12, refuse_null_primary
    ldr  x13, [x12, #0]
    ldr  x14, [x19, #0x90]                       // class_table[4] = WeaponItemData klass
    cbz  x14, refuse_not_configured
    bl   belmont_is_a                           // in: x13=klass, x14=base; out: w0
    cbz  w0, refuse_class_mismatch_primary

    bl   belmont_scan_owned
    cbz  w0, refuse_not_owned

    ldr  x0, [x19, #0x48]
    mov  x1, x12
    bl   belmont_inventory_equip_weapon
    str  xzr, [x19, #0x68]
    b    op_success

op_equip_charm:
    ldr  x9, [x19, #0x48]
    cbz  x9, refuse_null_context
    ldr  x10, [x9, #0]
    ldr  x11, [x19, #0xA8]
    cbz  x11, refuse_not_configured
    cmp  x10, x11
    b.ne refuse_null_context

    ldr  x12, [x19, #0x18]                      // primary = CharmItemData*
    cbz  x12, refuse_null_primary
    ldr  x13, [x12, #0]
    ldr  x14, [x19, #0x98]                        // class_table[5] = CharmItemData klass
    cbz  x14, refuse_not_configured
    bl   belmont_is_a                           // in: x13=klass, x14=base; out: w0
    cbz  w0, refuse_class_mismatch_primary

    bl   belmont_scan_owned
    cbz  w0, refuse_not_owned

    ldr  x0, [x19, #0x48]
    mov  x1, x12
    ldr  w2, [x19, #0x30]                           // req_arg_a = slot, -1 = auto
    bl   belmont_inventory_equip_charm
    sxtw x0, w0                                       // EquipCharmFromInventory returns int
    str  x0, [x19, #0x68]
    b    op_success

// ---------------------------------------------------------------------------
// belmont_is_a: IL2CPP klass parent walk (klass+0x58 = parent, the same link
// belmont_inventory.h follows to ItemData). Weapons are concrete subclasses
// (WeaponFistItemData, WeaponSpearItemData, ...) of the class_table base, so
// an exact klass compare would refuse every real weapon. Bounded to 8 hops.
//   in:  x13 = candidate klass, x14 = required base klass
//   out: w0 = 1 if x13 is x14 or derives from it, else 0. Clobbers x0,x13,x15.
// ---------------------------------------------------------------------------
belmont_is_a:
    mov  x15, #8
is_a_loop:
    cmp  x13, x14
    b.eq is_a_yes
    ldr  x13, [x13, #0x58]
    cbz  x13, is_a_no
    subs x15, x15, #1
    b.ne is_a_loop
is_a_no:
    mov  w0, #0
    ret
is_a_yes:
    mov  w0, #1
    ret

// ---------------------------------------------------------------------------
// belmont_scan_owned: bounded linear scan of Inventory._itemsInInventory
// (Inventory+0x20 = List<ItemData>: count at list+0x18, backing array at
// list+0x10, elements at array+0x20+8*i -- RE H5). Local subroutine, called only
// from within this same translation unit, so plain bl/ret is safe: x30 here is
// our OWN call chain, already saved on this helper's scratch frame, not the
// value Update() needs.
//   in:  x9 = Inventory*, x12 = candidate ItemData*
//   out: w0 = 1 if found, 0 otherwise. Clobbers x0,x1,x2,x15,x16,x17.
// ---------------------------------------------------------------------------
belmont_scan_owned:
    ldr  x15, [x9, #0x20]           // _itemsInInventory (List<ItemData>*)
    cbz  x15, scan_owned_not_found
    ldr  w16, [x15, #0x18]            // count, zero-extends into x16
    ldr  x17, [x15, #0x10]              // backing array
    cbz  x17, scan_owned_not_found
    add  x17, x17, #0x20                 // &elements[0]
    mov  x0, #0                            // i = 0
    mov  x1, #2048                           // hard safety cap, independent of count
scan_owned_loop:
    cmp  x0, x16
    b.ge scan_owned_not_found
    cmp  x0, x1
    b.ge scan_owned_not_found
    ldr  x2, [x17, x0, lsl #3]
    cmp  x2, x12
    b.eq scan_owned_found
    add  x0, x0, #1
    b    scan_owned_loop
scan_owned_found:
    mov  w0, #1
    ret
scan_owned_not_found:
    mov  w0, #0
    ret

// ---------------------------------------------------------------------------
// Refusals and the common tail. Every path here writes result_code and
// publishes result_seq = req_seq (release) exactly once, then restores every
// saved register and resumes Update() at 0x40D4F04.
// ---------------------------------------------------------------------------
refuse_bad_op:                    mov w0, #2;  b acknowledge_refuse
refuse_stale_epoch:               mov w0, #3;  b acknowledge_refuse
refuse_loading:                   mov w0, #4;  b acknowledge_refuse
refuse_loading_unknown:           mov w0, #4;  b acknowledge_refuse
refuse_null_context:              mov w0, #5;  b acknowledge_refuse
refuse_null_primary:              mov w0, #6;  b acknowledge_refuse
refuse_null_secondary:            mov w0, #7;  b acknowledge_refuse
refuse_null_tertiary:             mov w0, #8;  b acknowledge_refuse
refuse_class_mismatch_primary:    mov w0, #9;  b acknowledge_refuse
refuse_class_mismatch_secondary:  mov w0, #10; b acknowledge_refuse
refuse_class_mismatch_tertiary:   mov w0, #11; b acknowledge_refuse
refuse_not_owned:                 mov w0, #12; b acknowledge_refuse
refuse_alloc_failed:              mov w0, #13; b acknowledge_refuse
refuse_not_configured:            mov w0, #1;  b acknowledge_refuse

acknowledge_refuse:
    str  xzr, [x19, #0x68]       // result_value = 0 on every refusal path
    b    acknowledge_common

op_success:
    mov  w0, #0                    // result code OK; result_value already written
    b    acknowledge_common

acknowledge_common:
    str  w0, [x19, #0x64]          // result_code
    ldr  w9, [x19, #0x08]            // req_seq, reloaded fresh
    add  x10, x19, #0x60
    stlr w9, [x10]                    // result_seq = req_seq, RELEASE: publishes the above

helper_done:
    ldp  x0,  x30, [sp, #0x00]
    ldp  x19, x20, [sp, #0x10]
    ldp  x21, x22, [sp, #0x20]
    ldp  x23, x24, [sp, #0x30]
    ldp  x25, x26, [sp, #0x40]
    ldp  x27, x28, [sp, #0x50]
    ldp  d8,  d9,  [sp, #0x60]
    ldp  d10, d11, [sp, #0x70]
    ldp  d12, d13, [sp, #0x80]
    ldp  d14, d15, [sp, #0x90]
    add  sp, sp, #0xA0
    b    belmont_resume          // -> 0x40D4F04, resume Update() right after the patch
