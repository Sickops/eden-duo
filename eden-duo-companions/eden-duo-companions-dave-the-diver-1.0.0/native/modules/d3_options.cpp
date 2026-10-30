// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_options.h"

#include "d3_hg_draw.h"

namespace d3_options {
using namespace d3hg::draw;

Image Page(d3_assets::Assets& a, const EdenDsmodHostApi* h, bool tap_use) {
    Image out(PageW, PageH);
    d3_img::Blit(out, Ringside(a, h, OptW, OptH), OptX, OptY);
    TxtMid(out, a.exo, 45, PageW / 2, OptY + 62, "OPTIONS", Gold, 1);
    // the toggle row: native console checkbox, label beside it (the whole row is the hit area)
    const int mid = OptRowY + OptRowH / 2;
    const Image box = a.Frame(h, "2DUIConsoleOptions", tap_use ? "Console_CheckboxCheckedUp" : "Console_CheckboxUp");
    if (!box.Empty())
        BlitC(out, IconFit(box, 64, 64, true), OptRowX + 50, mid);
    TxtMid(out, a.sans, 36, OptRowX + 110, mid, "Tap a skill on the Map to use it", White);
    // what the setting changes, in the body colour of native option descriptions
    const char* note = tap_use
        ? "A tap presses the skill's button in game. Long-press a skill to read it."
        : "A tap shows the skill's description. Button presses never pop it up.";
    int y = OptRowY + OptRowH + 24;
    for (const auto& line : Wrap(a.sans, 30, note, OptW - 120, 3)) {
        Txt(out, a.sans, 30, OptX + 60, y, line, LGrey);
        y += LineH(a.sans, 30);
    }
    return out;
}
} // namespace d3_options
