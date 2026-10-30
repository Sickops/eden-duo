// SPDX-FileCopyrightText: Copyright 2026 Eden Duo Windows fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/duo/aux_window.h"

#include <algorithm>
#include <cmath>
#include <span>

#include <QCloseEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QPalette>
#include <QResizeEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>

#include "common/logging.h"
#include "core/core.h"
#include "core/frontend/graphics_context.h"
#include "qt_common/qt_common.h"
#include "qt_common/render/context.h"
#include "video_core/dsmod/aux_routing.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace {

/// Native child window that owns the Vulkan surface. Mirrors RenderWidget in bootmanager.cpp:
/// no paint engine, no Qt painting, the renderer writes the swapchain directly.
class AuxRenderWidget final : public QWidget {
public:
    explicit AuxRenderWidget(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);
        windowHandle()->setSurfaceType(QWindow::VulkanSurface);
    }

    QPaintEngine* paintEngine() const override {
        return nullptr;
    }
};

} // namespace

AuxRenderWindow::AuxRenderWindow(Core::System& system_, QScreen* target_screen_)
    : system{system_}, target_screen{target_screen_} {
    setWindowTitle(QStringLiteral("Eden - Second Screen (DSMod)"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    resize(640, 740);
}

AuxRenderWindow::~AuxRenderWindow() {
    Detach();
}

std::unique_ptr<Core::Frontend::GraphicsContext> AuxRenderWindow::CreateSharedContext() const {
    return std::make_unique<DummyContext>();
}

bool AuxRenderWindow::IsShown() const {
    return isVisible() && !isMinimized();
}

void AuxRenderWindow::CreateRenderSurface() {
    render_widget = new AuxRenderWidget(this);
    render_widget->setGeometry(rect());
    render_widget->windowHandle()->create();
    render_widget->show();
    window_info = QtCommon::GetWindowSystemInfo(render_widget->windowHandle());
}

void AuxRenderWindow::PositionOnTargetScreen() {
    if (target_screen != nullptr) {
        setGeometry(target_screen->availableGeometry());
        show();
        if (windowHandle() != nullptr) {
            windowHandle()->setScreen(target_screen);
        }
        showMaximized();
        return;
    }
    resize(640, 740);
    show();
}

void AuxRenderWindow::Attach() {
    if (attached) {
        show();
        return;
    }
    if (render_widget == nullptr) {
        CreateRenderSurface();
    }
    PositionOnTargetScreen();
    UpdateLayout();
    window_info = QtCommon::GetWindowSystemInfo(render_widget->windowHandle());
    attached = true;
    system.Renderer().SetAuxWindow(this);
    LOG_INFO(Frontend, "DSMod second screen attached ({}x{})", GetFramebufferLayout().width,
             GetFramebufferLayout().height);
}

void AuxRenderWindow::Detach() {
    if (attached) {
        attached = false;
        // The renderer may already be gone (forced stop): ShutdownMainProcess resets the GPU core,
        // and Renderer()/GPU() are not valid afterwards. Only touch them while they still exist.
        if (system.IsPoweredOn()) {
            system.Renderer().SetAuxWindow(nullptr);
        }
        LOG_INFO(Frontend, "DSMod second screen detached");
    }
    ResetInput();
    hide();
}

void AuxRenderWindow::UpdateLayout() {
    if (!system.IsPoweredOn()) {
        return;
    }
    const qreal dpr =
        render_widget != nullptr ? render_widget->devicePixelRatioF() : devicePixelRatioF();
    const u32 panel_width =
        std::max<u32>(1, static_cast<u32>(std::lround(this->width() * dpr)));
    const u32 panel_height =
        std::max<u32>(1, static_cast<u32>(std::lround(this->height() * dpr)));

    // The aux output always fills the whole window (no 16:9 letterboxing): the companion runtime
    // queries the real panel size through dsm:u and lays itself out accordingly.
    Layout::FramebufferLayout layout{};
    layout.width = panel_width;
    layout.height = panel_height;
    layout.screen = Common::Rectangle<u32>{0, 0, panel_width, panel_height};
    layout.is_srgb = false;
    NotifyFramebufferLayoutChanged(layout);

    auto& aux = system.GPU().DSModAux();
    aux.width.store(layout.width);
    aux.height.store(layout.height);
}

void AuxRenderWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (render_widget != nullptr) {
        render_widget->setGeometry(rect());
    }
    UpdateLayout();
    // Force the presenter to rebuild its swapchain when the panel size changed. SetAuxWindow is
    // idempotent for the same pointer; it only marks the aux state dirty.
    if (attached && system.IsPoweredOn()) {
        system.Renderer().SetAuxWindow(this);
    }
}

void AuxRenderWindow::closeEvent(QCloseEvent* event) {
    // Closing the second screen only hides it; emulation keeps running.
    Detach();
    if (close_callback) {
        close_callback();
    }
    event->accept();
}

void AuxRenderWindow::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    ResetInput();
}

void AuxRenderWindow::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    ResetInput();
}

void AuxRenderWindow::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    ResetInput();
}

void AuxRenderWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || mouse_down) {
        return;
    }
    mouse_down = true;
    mouse_pos = event->position();
    PublishTouch(true, true);
    event->accept();
}

void AuxRenderWindow::mouseMoveEvent(QMouseEvent* event) {
    mouse_pos = event->position();
    if (mouse_down) {
        PublishTouch(true, false);
    }
    event->accept();
}

void AuxRenderWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !mouse_down) {
        return;
    }
    mouse_down = false;
    PublishTouch(false, false);
    event->accept();
}

void AuxRenderWindow::wheelEvent(QWheelEvent* event) {
    // Own the wheel even when no companion pane accepts it: an aux wheel must never reach the
    // main game's mouse bindings.
    const auto [x, y] = ToPixels(event->position());
    float dx = static_cast<float>(event->angleDelta().x()) / 120.0f;
    float dy = static_cast<float>(event->angleDelta().y()) / 120.0f;
    if (dx == 0.0f && dy == 0.0f) {
        dx = static_cast<float>(event->pixelDelta().x()) / 120.0f;
        dy = static_cast<float>(event->pixelDelta().y()) / 120.0f;
    }
    if ((event->modifiers() & Qt::ShiftModifier) != 0 && dx == 0.0f) {
        dx = -dy;
        dy = 0.0f;
    }
    if (system.IsPoweredOn()) {
        system.GPU().DSModAux().PushWheel({x, y, dx, dy});
    }
    event->accept();
}

void AuxRenderWindow::PublishTouch(bool down, bool started) {
    if (!system.IsPoweredOn()) {
        return;
    }
    auto& aux = system.GPU().DSModAux();
    if (!down) {
        aux.SetTouch({});
        return;
    }
    const auto [x, y] = ToPixels(mouse_pos);
    const VideoCore::DSMod::AuxTouchPoint point{
        .finger_id = 0,
        .x = x,
        .y = y,
        .attributes = started ? 1u : 0u,
        .delta_ns = 0,
    };
    aux.SetTouch(std::span{&point, 1});
}

void AuxRenderWindow::ResetInput() {
    if (!mouse_down) {
        return;
    }
    mouse_down = false;
    if (system.IsPoweredOn()) {
        system.GPU().DSModAux().SetTouch({});
    }
}

std::pair<u32, u32> AuxRenderWindow::ToPixels(const QPointF& pos) const {
    const qreal dpr =
        render_widget != nullptr ? render_widget->devicePixelRatioF() : devicePixelRatioF();
    const auto& layout = GetFramebufferLayout();
    const u32 max_x = layout.width > 0 ? layout.width - 1 : 0;
    const u32 max_y = layout.height > 0 ? layout.height - 1 : 0;
    const u32 x = static_cast<u32>(std::clamp<qreal>(pos.x() * dpr, 0.0, max_x));
    const u32 y = static_cast<u32>(std::clamp<qreal>(pos.y() * dpr, 0.0, max_y));
    return {x, y};
}
