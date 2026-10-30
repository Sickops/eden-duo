// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Boot-free regression of NSO helper-page placement against the production NCE patcher.
// The patcher only emits AArch64 instructions, so this can also run on an x86 host.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/arm/nce/patcher.h"

namespace {
constexpr size_t Page = 0x1000;
constexpr size_t SvcOffset = 0x24;
constexpr u64 Base = 0x100000;

std::vector<u8> Image(size_t size) {
    std::vector<u8> image(size);
    constexpr u32 svc = 0xD4000021; // svc #1
    std::memcpy(image.data() + SvcOffset, &svc, sizeof(svc));
    return image;
}

u64 BranchTarget(const std::vector<u8>& image, u64 base) {
    u32 instruction{};
    std::memcpy(&instruction, image.data() + SvcOffset, sizeof(instruction));
    if ((instruction & 0xFC000000) != 0x14000000) {
        return 0;
    }
    const s64 displacement = static_cast<s64>(static_cast<s32>(instruction << 6) >> 6) * 4;
    return base + SvcOffset + displacement;
}

// Model the NSO loader's two passes, using the actual PatchText/RelocateAndCopy implementation.
// late_helper reproduces 1.2.1: helpers were absent from PatchText's size accounting and were
// appended after RelocateAndCopy. The normal path reserves them before both calls.
bool Check(std::string_view label, int helper_module, size_t mailbox_size, bool late_helper,
           bool separate_patchers = false, bool nce = true) {
    constexpr std::array<size_t, 3> NativeSizes{Page, 2 * Page, 3 * Page};
    constexpr Kernel::CodeSet::Segment Text{0, 0, Page};
    constexpr size_t HelperSize = 2 * Page;
    std::array<size_t, 3> sizes = NativeSizes;
    sizes.back() += mailbox_size;
    if (!late_helper && helper_module >= 0) {
        sizes[helper_module] += HelperSize;
    }
    Core::NCE::Patcher shared;
    std::array<Core::NCE::Patcher, 3> separate;
    std::array<Core::NCE::Patcher*, 3> patchers{};
    for (size_t i = 0; i < sizes.size(); ++i) {
        patchers[i] = separate_patchers ? &separate[i] : &shared;
        if (nce && !patchers[i]->PatchText(Image(sizes[i]), Text)) {
            return false;
        }
    }
    std::array<std::vector<u8>, 3> images;
    std::array<u64, 3> bases{};
    std::array<u64, 3> targets{};
    std::array<u64, 3> patch_starts{};
    u64 next = Base;
    Core::NCE::EntryTrampolines trampolines;
    for (size_t i = 0; i < sizes.size(); ++i) {
        bases[i] = next;
        images[i] = Image(sizes[i]);
        const size_t before = images[i].size();
        if (nce) {
            const bool copied = patchers[i]->RelocateAndCopy(next, Text, images[i], &trampolines);
            if (copied) {
                patch_starts[i] = next + before;
            }
            targets[i] = BranchTarget(images[i], next);
        }
        if (late_helper && static_cast<int>(i) == helper_module) {
            images[i].resize(images[i].size() + HelperSize);
        }
        next += images[i].size();
    }
    bool passed = true;
    s64 first_delta{};
    if (nce) {
        for (size_t i = 0; i < sizes.size(); ++i) {
            const size_t host = separate_patchers ? i : sizes.size() - 1;
            const u64 start = patch_starts[host];
            const u64 end = start + patchers[i]->GetSectionSize();
            // Every SVC must branch into mapped RX patch pages, never SDK data/helper pages.
            passed &= targets[i] >= start && targets[i] < end;
            if (i == 0) {
                first_delta = static_cast<s64>(targets[i]) - static_cast<s64>(start);
            }
        }
    } else {
        passed = next == Base + 6 * Page + mailbox_size +
                                           (helper_module >= 0 ? HelperSize : 0);
    }
    std::cout << label << ": " << (passed ? "PASS" : "FAIL")
              << " rtld_target_minus_patch_start=" << first_delta << '\n';
    return passed;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "--reproduce-1.2.1") {
        return Check("1.2.1 helper on main", 1, Page, true) ? 0 : 1;
    }
    bool passed = true;
    passed &= Check("NCE no plan", -1, 0, false);
    passed &= Check("NCE mailbox-only plan", -1, Page, false);
    passed &= Check("NCE helper on main", 1, Page, false);
    passed &= Check("NCE helper on last module", 2, Page, false);
    passed &= Check("NCE helper with separate patchers", 1, Page, false, true);
    passed &= Check("Dynarmic no plan", -1, 0, false, false, false);
    passed &= Check("Dynarmic mailbox-only plan", -1, Page, false, false, false);
    passed &= Check("Dynarmic helper on main", 1, Page, false, false, false);
    passed &= Check("Dynarmic helper on last module", 2, Page, false, false, false);
    return passed ? 0 : 1;
}
