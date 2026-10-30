// SPDX-FileCopyrightText: Copyright 2026 Eden Duo Windows fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Second ("aux") desktop window for the Qt frontend. It is the Windows counterpart of
// AuxWindow_SDL3 in eden-cli: a bare Core::Frontend::EmuWindow whose surface the Vulkan
// renderer presents the DSMod companion output to, and whose mouse input is published to
// VideoCore::DSMod::AuxRouting for the dsm:u service.

#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <QPointF>
#include <QWidget>

#include "core/frontend/emu_window.h"

class QCloseEvent;
class QFocusEvent;
class QHideEvent;
class QMouseEvent;
class QResizeEvent;
class QScreen;
class QWheelEvent;

namespace Core {
class System;
}

class AuxRenderWindow final : public QWidget, public Core::Frontend::EmuWindow {
public:
    explicit AuxRenderWindow(Core::System& system_, QScreen* target_screen = nullptr);
    ~AuxRenderWindow() override;

    // EmuWindow implementation.
    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override;
    bool IsShown() const override;

    /// Creates the native surface, shows the window and attaches it to the renderer.
    void Attach();
    /// Detaches from the renderer (synchronous) and hides the window.
    void Detach();

    /// True when the window is currently attached to the renderer.
    bool IsAttached() const {
        return attached;
    }

    /// Invoked when the user closes the window. The window only hides; emulation keeps running.
    void SetCloseCallback(std::function<void()> callback) {
        close_callback = std::move(callback);
    }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void CreateRenderSurface();
    void UpdateLayout();
    void PositionOnTargetScreen();
    void PublishTouch(bool down, bool started);
    void ResetInput();
    std::pair<u32, u32> ToPixels(const QPointF& pos) const;

    Core::System& system;
    QScreen* target_screen{};
    QWidget* render_widget{};
    std::function<void()> close_callback;
    bool attached{};
    bool mouse_down{};
    QPointF mouse_pos{};
};
